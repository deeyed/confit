#include "confit/build_policy.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/generator_v2.h"

enum {
  CONFIT_BUILD_POLICY_TEXT_LIMIT = 32768,
  CONFIT_BUILD_ACTION_HEADER_SIZE = 32,
  CONFIT_BUILD_ACTION_ID_SIZE = 64,
  CONFIT_BUILD_ACTION_NOFOLLOW_FLAG = 1,
};

typedef struct ConfitBuildTextBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitBuildTextBuilder;

typedef struct ConfitBuildWireWriter {
  unsigned char *bytes;
  size_t capacity;
  size_t cursor;
} ConfitBuildWireWriter;

typedef struct ConfitBuildWireReader {
  const unsigned char *bytes;
  size_t size;
  size_t cursor;
} ConfitBuildWireReader;

typedef struct ConfitBuildWireSpan {
  const unsigned char *bytes;
  size_t size;
} ConfitBuildWireSpan;

typedef struct ConfitBuildWireEndpoint {
  ConfitBuildRole role;
  ConfitBuildWireSpan path;
  uint64_t maximum_bytes;
} ConfitBuildWireEndpoint;

static int confit_build_span_equal_text(const ConfitBuildWireSpan *span,
                                        const char *text);

static const char *const kDomainNames[] = {
    "invalid", "toolgen", "envgen", "kerngen", "worldgen", "imagegen",
    "test"};

static const char *const kKindNames[] = {
    "invalid", "mkdir", "clean", "copy", "generate", "compile",
    "archive", "link", "package", "verify", "publish", "execute_test"};

static const char *const kRoleNames[] = {
    "invalid",
    "host.source", "bootstrap.tool", "tool.executable", "tool.seal",
    "tool.root", "env.source", "env.header", "env.crt", "env.library",
    "env.sysroot.seal", "env.root", "kernel.source", "kernel.object",
    "kernel.elf", "kernel.metadata", "kernel.seal", "kernel.root",
    "world.source", "world.object", "world.artifact", "world.install_plan",
    "world.seal", "world.root", "image.policy", "image.artifact",
    "image.package", "image.seal", "image.root", "test.descriptor",
    "test.image", "test.log", "test.receipt", "test.root"};

static const char *const kToolRoleNames[] = {
    "invalid", "builtin", "host.cc", "target.cc", "target.ar",
    "target.ld", "dtc", "qemu"};

static int confit_build_sha256_valid(const char *text) {
  size_t index;
  if (text == 0 || strlen(text) != 64U) return 0;
  for (index = 0U; index < 64U; ++index) {
    if (!((text[index] >= '0' && text[index] <= '9') ||
          (text[index] >= 'a' && text[index] <= 'f'))) return 0;
  }
  return 1;
}

static int confit_build_atom_valid(const char *text) {
  size_t index;
  const size_t size = text != 0 ? strlen(text) : 0U;
  if (size == 0U || size > CONFIT_BUILD_ACTION_MAX_ATOM_BYTES) return 0;
  for (index = 0U; index < size; ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (!((value >= 'a' && value <= 'z') ||
          (value >= 'A' && value <= 'Z') ||
          (value >= '0' && value <= '9') || value == '.' || value == '_' ||
          value == '-' || value == '+' || value == '@')) return 0;
  }
  return 1;
}

static int confit_build_relative_path_valid(const char *text) {
  const char *segment;
  const char *cursor;
  size_t size;
  if (text == 0) return 0;
  size = strlen(text);
  if (size == 0U || size > CONFIT_BUILD_ACTION_MAX_PATH_BYTES ||
      text[0] == '/' || text[0] == '\\' || strchr(text, '\\') != 0) return 0;
  segment = text;
  cursor = text;
  for (;;) {
    if (*cursor == '/' || *cursor == '\0') {
      const size_t segment_size = (size_t)(cursor - segment);
      if (segment_size == 0U ||
          (segment_size == 1U && segment[0] == '.') ||
          (segment_size == 2U && segment[0] == '.' && segment[1] == '.')) {
        return 0;
      }
      if (*cursor == '\0') break;
      segment = cursor + 1;
    } else {
      const unsigned char value = (unsigned char)*cursor;
      if (!((value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '.' || value == '_' ||
            value == '-' || value == '+' || value == '@')) return 0;
    }
    cursor += 1;
  }
  return 1;
}

static int confit_build_absolute_path_valid(const char *text) {
  const char *segment;
  const char *cursor;
  size_t size;
  if (text == 0 || text[0] != '/' || text[1] == '\0' ||
      strchr(text, '\\') != 0) return 0;
  size = strlen(text);
  if (size > CONFIT_BUILD_ACTION_MAX_PATH_BYTES) return 0;
  segment = text + 1;
  cursor = segment;
  for (;;) {
    if (*cursor == '/' || *cursor == '\0') {
      const size_t segment_size = (size_t)(cursor - segment);
      if (segment_size == 0U ||
          (segment_size == 1U && segment[0] == '.') ||
          (segment_size == 2U && segment[0] == '.' && segment[1] == '.')) {
        return 0;
      }
      if (*cursor == '\0') break;
      segment = cursor + 1;
    } else {
      const unsigned char value = (unsigned char)*cursor;
      if (!((value >= 'a' && value <= 'z') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || value == '.' || value == '_' ||
            value == '-' || value == '+' || value == '@')) return 0;
    }
    cursor += 1;
  }
  return 1;
}

static int confit_build_domain_valid(ConfitBuildDomain value) {
  return value >= CONFIT_BUILD_DOMAIN_TOOLGEN &&
         value <= CONFIT_BUILD_DOMAIN_TEST;
}

static int confit_build_kind_valid(ConfitBuildActionKind value) {
  return value >= CONFIT_BUILD_ACTION_KIND_MKDIR &&
         value <= CONFIT_BUILD_ACTION_KIND_EXECUTE_TEST;
}

static int confit_build_role_valid(ConfitBuildRole value) {
  return value >= CONFIT_BUILD_ROLE_HOST_SOURCE &&
         value <= CONFIT_BUILD_ROLE_TEST_ROOT;
}

static int confit_build_tool_role_valid(ConfitBuildToolRole value) {
  return value >= CONFIT_BUILD_TOOL_BUILTIN && value <= CONFIT_BUILD_TOOL_QEMU;
}

const char *confit_build_domain_name(ConfitBuildDomain domain) {
  return domain >= CONFIT_BUILD_DOMAIN_INVALID &&
                 domain <= CONFIT_BUILD_DOMAIN_TEST
             ? kDomainNames[(size_t)domain]
             : "invalid";
}

const char *confit_build_action_kind_name(ConfitBuildActionKind kind) {
  return kind >= CONFIT_BUILD_ACTION_KIND_INVALID &&
                 kind <= CONFIT_BUILD_ACTION_KIND_EXECUTE_TEST
             ? kKindNames[(size_t)kind]
             : "invalid";
}

const char *confit_build_role_name(ConfitBuildRole role) {
  return role >= CONFIT_BUILD_ROLE_INVALID && role <= CONFIT_BUILD_ROLE_TEST_ROOT
             ? kRoleNames[(size_t)role]
             : "invalid";
}

const char *confit_build_tool_role_name(ConfitBuildToolRole role) {
  return role >= CONFIT_BUILD_TOOL_INVALID && role <= CONFIT_BUILD_TOOL_QEMU
             ? kToolRoleNames[(size_t)role]
             : "invalid";
}

static int confit_build_text_append(ConfitBuildTextBuilder *builder,
                                    const char *format, ...) {
  va_list arguments;
  int written;
  if (builder->text == 0 || builder->size >= builder->capacity) return 0;
  va_start(arguments, format);
  written = vsnprintf(builder->text + builder->size,
                      builder->capacity - builder->size, format, arguments);
  va_end(arguments);
  if (written < 0 || (size_t)written >= builder->capacity - builder->size) {
    return 0;
  }
  builder->size += (size_t)written;
  return 1;
}

static int confit_build_role_list_valid(char *const *roles, size_t count) {
  size_t index;
  size_t prior;
  if (roles == 0 || count == 0U || count > CONFIT_BUILD_ACTION_MAX_OUTPUTS) {
    return 0;
  }
  for (index = 0U; index < count; ++index) {
    const char *separator = roles[index] != 0 ? strchr(roles[index], '=') : 0;
    char role[CONFIT_BUILD_ACTION_MAX_ATOM_BYTES + 1U];
    char path[CONFIT_BUILD_ACTION_MAX_PATH_BYTES + 1U];
    const size_t role_size = separator != 0
                                 ? (size_t)(separator - roles[index])
                                 : 0U;
    const size_t path_size = separator != 0 ? strlen(separator + 1) : 0U;
    if (separator == 0 || role_size == 0U ||
        role_size > CONFIT_BUILD_ACTION_MAX_ATOM_BYTES ||
        strchr(separator + 1, '=') != 0 || path_size == 0U ||
        path_size > CONFIT_BUILD_ACTION_MAX_PATH_BYTES) return 0;
    memcpy(role, roles[index], role_size);
    role[role_size] = '\0';
    memcpy(path, separator + 1, path_size + 1U);
    if (!confit_build_atom_valid(role) ||
        !confit_build_relative_path_valid(path)) return 0;
    for (prior = 0U; prior < index; ++prior) {
      const char *prior_separator = strchr(roles[prior], '=');
      if (prior_separator != 0 &&
          (size_t)(separator - roles[index]) ==
              (size_t)(prior_separator - roles[prior]) &&
          memcmp(roles[index], roles[prior],
                 (size_t)(separator - roles[index])) == 0) return 0;
    }
  }
  return 1;
}

static int confit_build_plan_valid(const ConfitTargetPlan *plan) {
  if (plan == 0 || !confit_build_atom_valid(plan->target_id) ||
      !confit_build_atom_valid(plan->isa) ||
      !confit_build_atom_valid(plan->abi) ||
      !confit_build_atom_valid(plan->cpu_profile) ||
      !confit_build_atom_valid(plan->entry_profile) ||
      !confit_build_atom_valid(plan->toolchain_id) ||
      !confit_build_atom_valid(plan->toolchain_kind) ||
      !confit_build_atom_valid(plan->target_triple) ||
      !confit_build_absolute_path_valid(plan->compiler_path) ||
      !confit_build_sha256_valid(plan->compiler_sha256) ||
      !confit_build_atom_valid(plan->compiler_version) ||
      !confit_build_absolute_path_valid(plan->archiver_path) ||
      !confit_build_sha256_valid(plan->archiver_sha256) ||
      !confit_build_atom_valid(plan->archiver_version) ||
      !confit_build_absolute_path_valid(plan->linker_path) ||
      !confit_build_sha256_valid(plan->linker_sha256) ||
      !confit_build_atom_valid(plan->linker_version) ||
      !confit_build_atom_valid(plan->support_provider_owner) ||
      !confit_build_atom_valid(plan->support_consumer_owner) ||
      !confit_build_atom_valid(plan->expected_component) ||
      strcmp(plan->support_consumer_owner, plan->expected_component) != 0 ||
      strcmp(plan->support_provider_owner, plan->support_consumer_owner) == 0 ||
      !confit_build_atom_valid(plan->support_role) ||
      !confit_build_atom_valid(plan->support_required_kapi) ||
      strcmp(plan->support_role, "architecture.facade.v1") != 0 ||
      !confit_build_relative_path_valid(plan->support_facade_include_root) ||
      strncmp(plan->support_facade_include_root, "sys/include/parus/", 18U) != 0 ||
      !confit_build_role_list_valid(plan->kernel_artifact_roles,
                                    plan->kernel_artifact_role_count) ||
      !confit_build_role_list_valid(plan->image_artifact_roles,
                                    plan->image_artifact_role_count)) return 0;
  if (plan->dtc_path != 0 &&
      (!confit_build_absolute_path_valid(plan->dtc_path) ||
       !confit_build_sha256_valid(plan->dtc_sha256) ||
       !confit_build_atom_valid(plan->dtc_version))) return 0;
  if (strcmp(plan->world_artifact_profile, "none") != 0 &&
      !confit_build_role_list_valid(plan->world_artifact_roles,
                                    plan->world_artifact_role_count)) return 0;
  if (plan->machine_executable != 0) {
    if (!confit_build_atom_valid(plan->machine_trust_profile) ||
        !confit_build_atom_valid(plan->machine_resource_identity) ||
        !confit_build_atom_valid(plan->machine_evidence_transport) ||
        !confit_build_atom_valid(plan->machine_evidence_protocol) ||
        strcmp(plan->machine_trust_profile, "qemu-executable-v1") != 0 ||
        strcmp(plan->machine_evidence_transport,
               "qemu-fwcfg-challenge-v1") != 0 ||
        strcmp(plan->machine_evidence_protocol,
               "parus-qemu-terminal-v1") != 0 ||
        !confit_build_absolute_path_valid(plan->machine_executable_path) ||
        !confit_build_sha256_valid(plan->machine_executable_sha256) ||
        !confit_build_atom_valid(plan->machine_executable_version) ||
        plan->machine_evidence_max_bytes == 0U ||
        plan->machine_evidence_max_bytes > 1024U * 1024U) return 0;
  }
  return 1;
}

static int confit_build_append_artifacts(ConfitBuildTextBuilder *builder,
                                         const char *prefix,
                                         char *const *roles, size_t count) {
  size_t index;
  for (index = 0U; index < count; ++index) {
    if (!confit_build_text_append(builder, "%s.%zu=%s\n", prefix, index,
                                  roles[index])) return 0;
  }
  return 1;
}

ConfitStatus confit_build_policy_generate(
    const char *profile_id, const char *configuration_sha256,
    const ConfitTargetPlan *target_plan, const char *target_sha256,
    char **out_policy, char out_policy_sha256[65],
    ConfitDiagnostic *diagnostic) {
  ConfitBuildTextBuilder builder;
  size_t index;
  if (out_policy == 0 || out_policy_sha256 == 0) return CONFIT_ERR_INVALID_ARGUMENT;
  *out_policy = 0;
  out_policy_sha256[0] = '\0';
  if (!confit_build_atom_valid(profile_id) ||
      !confit_build_sha256_valid(configuration_sha256) ||
      !confit_build_sha256_valid(target_sha256) ||
      !confit_build_plan_valid(target_plan)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, "build-policy", 0U,
                          0U, "selected build policy input is incomplete or unsafe");
    return CONFIT_ERR_SCHEMA;
  }
  builder.text = (char *)calloc(CONFIT_BUILD_POLICY_TEXT_LIMIT, 1U);
  builder.size = 0U;
  builder.capacity = CONFIT_BUILD_POLICY_TEXT_LIMIT;
  if (builder.text == 0) return CONFIT_ERR_INTERNAL;
  if (!confit_build_text_append(
          &builder,
          "schema=parus-build-policy-v1\npolicy_abi=%u\nprofile_id=%s\n"
          "configuration_sha256=%s\ntarget_id=%s\ntarget_sha256=%s\n"
          "target.isa=%s\ntarget.abi=%s\ntarget.cpu_profile=%s\n"
          "target.entry_profile=%s\n"
          "edge_table_id=parus-five-gen-edge-v1\n"
          "toolchain_plan_sha256=%s\n"
          "toolchain.id=%s\ntoolchain.kind=%s\ntoolchain.triple=%s\n"
          "tool.target_cc.path=%s\ntool.target_cc.sha256=%s\n"
          "tool.target_cc.version=%s\n"
          "tool.target_ar.path=%s\ntool.target_ar.sha256=%s\n"
          "tool.target_ar.version=%s\n"
          "tool.target_ld.path=%s\ntool.target_ld.sha256=%s\n"
          "tool.target_ld.version=%s\n",
          (unsigned)CONFIT_BUILD_POLICY_ABI, profile_id,
          configuration_sha256, target_plan->target_id, target_sha256,
          target_plan->isa, target_plan->abi, target_plan->cpu_profile,
          target_plan->entry_profile, target_sha256,
          target_plan->toolchain_id, target_plan->toolchain_kind,
          target_plan->target_triple, target_plan->compiler_path,
          target_plan->compiler_sha256, target_plan->compiler_version,
          target_plan->archiver_path, target_plan->archiver_sha256,
          target_plan->archiver_version, target_plan->linker_path,
          target_plan->linker_sha256, target_plan->linker_version)) {
    free(builder.text);
    return CONFIT_ERR_INTERNAL;
  }
  for (index = CONFIT_BUILD_DOMAIN_TOOLGEN;
       index <= CONFIT_BUILD_DOMAIN_TEST; ++index) {
    if (!confit_build_text_append(&builder, "domain.%zu=%s\n", index - 1U,
                                  confit_build_domain_name(
                                      (ConfitBuildDomain)index))) goto overflow;
  }
  for (index = CONFIT_BUILD_ACTION_KIND_MKDIR;
       index <= CONFIT_BUILD_ACTION_KIND_EXECUTE_TEST; ++index) {
    if (!confit_build_text_append(&builder, "action_kind.%zu=%s\n",
                                  index - 1U,
                                  confit_build_action_kind_name(
                                      (ConfitBuildActionKind)index))) goto overflow;
  }
  for (index = CONFIT_BUILD_ROLE_HOST_SOURCE;
       index <= CONFIT_BUILD_ROLE_TEST_ROOT; ++index) {
    if (!confit_build_text_append(&builder, "role.%zu=%s\n", index - 1U,
                                  confit_build_role_name(
                                      (ConfitBuildRole)index))) goto overflow;
  }
  for (index = CONFIT_BUILD_TOOL_BUILTIN;
       index <= CONFIT_BUILD_TOOL_QEMU; ++index) {
    if (!confit_build_text_append(&builder, "tool_role.%zu=%s\n", index - 1U,
                                  confit_build_tool_role_name(
                                      (ConfitBuildToolRole)index))) goto overflow;
  }
  if (!confit_build_text_append(
          &builder,
          "output_root.toolgen=toolgen\noutput_root.envgen=envgen\n"
          "output_root.kerngen=kerngen\noutput_root.worldgen=worldgen\n"
          "output_root.imagegen=imagegen\noutput_root.test=test\n"
          "support.provider_owner=%s\nsupport.consumer_owner=%s\n"
          "support.role=%s\nsupport.facade_include_root=%s\n"
          "support.required_kapi=%s\n",
          target_plan->support_provider_owner,
          target_plan->support_consumer_owner, target_plan->support_role,
          target_plan->support_facade_include_root,
          target_plan->support_required_kapi)) goto overflow;
  if (!confit_build_append_artifacts(&builder, "artifact.kernel",
                                     target_plan->kernel_artifact_roles,
                                     target_plan->kernel_artifact_role_count) ||
      !confit_build_append_artifacts(&builder, "artifact.image",
                                     target_plan->image_artifact_roles,
                                     target_plan->image_artifact_role_count) ||
      (strcmp(target_plan->world_artifact_profile, "none") != 0 &&
       !confit_build_append_artifacts(&builder, "artifact.world",
                                      target_plan->world_artifact_roles,
                                      target_plan->world_artifact_role_count))) {
    goto overflow;
  }
  if (target_plan->machine_executable != 0) {
    if (!confit_build_text_append(
            &builder,
            "machine.trust_profile=%s\nmachine.executable_path=%s\n"
            "machine.executable_sha256=%s\nmachine.executable_version=%s\n"
            "machine.resource_identity=%s\nmachine.evidence_transport=%s\n"
            "machine.evidence_protocol=%s\nmachine.evidence_max_bytes=%zu\n",
            target_plan->machine_trust_profile,
            target_plan->machine_executable_path,
            target_plan->machine_executable_sha256,
            target_plan->machine_executable_version,
            target_plan->machine_resource_identity,
            target_plan->machine_evidence_transport,
            target_plan->machine_evidence_protocol,
            target_plan->machine_evidence_max_bytes)) goto overflow;
  } else if (!confit_build_text_append(
                 &builder,
                 "machine.trust_profile=none\n"
                 "machine.executable_path=none\n"
                 "machine.executable_sha256=none\n"
                 "machine.executable_version=none\n"
                 "machine.resource_identity=none\n"
                 "machine.evidence_transport=none\n"
                 "machine.evidence_protocol=none\n"
                 "machine.evidence_max_bytes=0\n")) {
    goto overflow;
  }
  if (target_plan->dtc_path != 0 &&
      !confit_build_text_append(
          &builder,
          "tool.dtc.path=%s\ntool.dtc.sha256=%s\ntool.dtc.version=%s\n",
          target_plan->dtc_path, target_plan->dtc_sha256,
          target_plan->dtc_version)) goto overflow;
  confit_v4_sha256_hex(builder.text, out_policy_sha256);
  *out_policy = builder.text;
  return CONFIT_OK;

overflow:
  free(builder.text);
  confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, "build-policy", 0U,
                        0U, "selected build policy exceeds its bounded form");
  return CONFIT_ERR_SCHEMA;
}

ConfitStatus confit_build_policy_generate_make_adapter(
    const char *policy_sha256, const char *configuration_sha256,
    const char *target_sha256, char **out_make,
    ConfitDiagnostic *diagnostic) {
  char *text;
  int written;
  if (out_make == 0) return CONFIT_ERR_INVALID_ARGUMENT;
  *out_make = 0;
  if (!confit_build_sha256_valid(policy_sha256) ||
      !confit_build_sha256_valid(configuration_sha256) ||
      !confit_build_sha256_valid(target_sha256)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                          "build-policy.mk", 0U, 0U,
                          "build policy adapter identity is invalid");
    return CONFIT_ERR_SCHEMA;
  }
  text = (char *)malloc(512U);
  if (text == 0) return CONFIT_ERR_INTERNAL;
  written = snprintf(
      text, 512U,
      "# Generated by Confit; build policy identity only.\n"
      "PARUS_BUILD_POLICY_ABI:= %u\nPARUS_BUILD_POLICY_SHA256:= %s\n"
      "PARUS_BUILD_CONFIGURATION_SHA256:= %s\n"
      "PARUS_BUILD_TARGET_SHA256:= %s\n"
      "PARUS_BUILD_EDGE_TABLE_ID:= parus-five-gen-edge-v1\n",
      (unsigned)CONFIT_BUILD_POLICY_ABI, policy_sha256,
      configuration_sha256, target_sha256);
  if (written < 0 || written >= 512) {
    free(text);
    return CONFIT_ERR_INTERNAL;
  }
  *out_make = text;
  return CONFIT_OK;
}

static int confit_build_kind_allowed(ConfitBuildDomain domain,
                                     ConfitBuildActionKind kind) {
  if (kind == CONFIT_BUILD_ACTION_KIND_MKDIR ||
      kind == CONFIT_BUILD_ACTION_KIND_CLEAN ||
      kind == CONFIT_BUILD_ACTION_KIND_VERIFY ||
      kind == CONFIT_BUILD_ACTION_KIND_PUBLISH) return 1;
  switch (domain) {
    case CONFIT_BUILD_DOMAIN_TOOLGEN:
      return kind == CONFIT_BUILD_ACTION_KIND_GENERATE ||
             kind == CONFIT_BUILD_ACTION_KIND_COMPILE ||
             kind == CONFIT_BUILD_ACTION_KIND_LINK;
    case CONFIT_BUILD_DOMAIN_ENVGEN:
      return kind == CONFIT_BUILD_ACTION_KIND_GENERATE ||
             kind == CONFIT_BUILD_ACTION_KIND_COMPILE ||
             kind == CONFIT_BUILD_ACTION_KIND_ARCHIVE;
    case CONFIT_BUILD_DOMAIN_KERNGEN:
      return kind == CONFIT_BUILD_ACTION_KIND_GENERATE ||
             kind == CONFIT_BUILD_ACTION_KIND_COMPILE ||
             kind == CONFIT_BUILD_ACTION_KIND_LINK;
    case CONFIT_BUILD_DOMAIN_WORLDGEN:
      return kind == CONFIT_BUILD_ACTION_KIND_COPY ||
             kind == CONFIT_BUILD_ACTION_KIND_COMPILE ||
             kind == CONFIT_BUILD_ACTION_KIND_ARCHIVE ||
             kind == CONFIT_BUILD_ACTION_KIND_LINK;
    case CONFIT_BUILD_DOMAIN_IMAGEGEN:
      return kind == CONFIT_BUILD_ACTION_KIND_COPY ||
             kind == CONFIT_BUILD_ACTION_KIND_GENERATE ||
             kind == CONFIT_BUILD_ACTION_KIND_PACKAGE;
    case CONFIT_BUILD_DOMAIN_TEST:
      return kind == CONFIT_BUILD_ACTION_KIND_COMPILE ||
             kind == CONFIT_BUILD_ACTION_KIND_LINK ||
             kind == CONFIT_BUILD_ACTION_KIND_EXECUTE_TEST;
    default:
      return 0;
  }
}

static int confit_build_input_role_allowed(ConfitBuildDomain domain,
                                           ConfitBuildRole role) {
  switch (domain) {
    case CONFIT_BUILD_DOMAIN_TOOLGEN:
      return role == CONFIT_BUILD_ROLE_HOST_SOURCE ||
             role == CONFIT_BUILD_ROLE_BOOTSTRAP_TOOL ||
             role == CONFIT_BUILD_ROLE_TOOL_EXECUTABLE ||
             role == CONFIT_BUILD_ROLE_TOOL_SEAL ||
             role == CONFIT_BUILD_ROLE_TOOL_ROOT;
    case CONFIT_BUILD_DOMAIN_ENVGEN:
      return role == CONFIT_BUILD_ROLE_TOOL_EXECUTABLE ||
             role == CONFIT_BUILD_ROLE_TOOL_SEAL ||
             role == CONFIT_BUILD_ROLE_ENV_SOURCE ||
             role == CONFIT_BUILD_ROLE_ENV_HEADER ||
             role == CONFIT_BUILD_ROLE_ENV_CRT ||
             role == CONFIT_BUILD_ROLE_ENV_LIBRARY ||
             role == CONFIT_BUILD_ROLE_ENV_SYSROOT_SEAL ||
             role == CONFIT_BUILD_ROLE_ENV_ROOT;
    case CONFIT_BUILD_DOMAIN_KERNGEN:
      return role == CONFIT_BUILD_ROLE_TOOL_EXECUTABLE ||
             role == CONFIT_BUILD_ROLE_TOOL_SEAL ||
             role == CONFIT_BUILD_ROLE_ENV_HEADER ||
             role == CONFIT_BUILD_ROLE_ENV_CRT ||
             role == CONFIT_BUILD_ROLE_ENV_LIBRARY ||
             role == CONFIT_BUILD_ROLE_ENV_SYSROOT_SEAL ||
             role == CONFIT_BUILD_ROLE_KERNEL_SOURCE ||
             role == CONFIT_BUILD_ROLE_KERNEL_OBJECT ||
             role == CONFIT_BUILD_ROLE_KERNEL_ELF ||
             role == CONFIT_BUILD_ROLE_KERNEL_METADATA ||
             role == CONFIT_BUILD_ROLE_KERNEL_SEAL ||
             role == CONFIT_BUILD_ROLE_KERNEL_ROOT;
    case CONFIT_BUILD_DOMAIN_WORLDGEN:
      return role == CONFIT_BUILD_ROLE_TOOL_EXECUTABLE ||
             role == CONFIT_BUILD_ROLE_TOOL_SEAL ||
             role == CONFIT_BUILD_ROLE_ENV_HEADER ||
             role == CONFIT_BUILD_ROLE_ENV_CRT ||
             role == CONFIT_BUILD_ROLE_ENV_LIBRARY ||
             role == CONFIT_BUILD_ROLE_ENV_SYSROOT_SEAL ||
             role == CONFIT_BUILD_ROLE_WORLD_SOURCE ||
             role == CONFIT_BUILD_ROLE_WORLD_OBJECT ||
             role == CONFIT_BUILD_ROLE_WORLD_ARTIFACT ||
             role == CONFIT_BUILD_ROLE_WORLD_INSTALL_PLAN ||
             role == CONFIT_BUILD_ROLE_WORLD_SEAL ||
             role == CONFIT_BUILD_ROLE_WORLD_ROOT;
    case CONFIT_BUILD_DOMAIN_IMAGEGEN:
      return role == CONFIT_BUILD_ROLE_TOOL_EXECUTABLE ||
             role == CONFIT_BUILD_ROLE_TOOL_SEAL ||
             role == CONFIT_BUILD_ROLE_KERNEL_ELF ||
             role == CONFIT_BUILD_ROLE_KERNEL_SEAL ||
             role == CONFIT_BUILD_ROLE_WORLD_ARTIFACT ||
             role == CONFIT_BUILD_ROLE_WORLD_INSTALL_PLAN ||
             role == CONFIT_BUILD_ROLE_WORLD_SEAL ||
             role == CONFIT_BUILD_ROLE_IMAGE_POLICY ||
             role == CONFIT_BUILD_ROLE_IMAGE_ARTIFACT ||
             role == CONFIT_BUILD_ROLE_IMAGE_ROOT;
    case CONFIT_BUILD_DOMAIN_TEST:
      return role == CONFIT_BUILD_ROLE_HOST_SOURCE ||
             role == CONFIT_BUILD_ROLE_TOOL_EXECUTABLE ||
             role == CONFIT_BUILD_ROLE_TOOL_SEAL ||
             role == CONFIT_BUILD_ROLE_TEST_DESCRIPTOR ||
             role == CONFIT_BUILD_ROLE_TEST_IMAGE ||
             role == CONFIT_BUILD_ROLE_KERNEL_ELF ||
             role == CONFIT_BUILD_ROLE_KERNEL_METADATA ||
             role == CONFIT_BUILD_ROLE_KERNEL_SEAL ||
             role == CONFIT_BUILD_ROLE_WORLD_ARTIFACT ||
             role == CONFIT_BUILD_ROLE_WORLD_INSTALL_PLAN ||
             role == CONFIT_BUILD_ROLE_WORLD_SEAL ||
             role == CONFIT_BUILD_ROLE_IMAGE_ARTIFACT ||
             role == CONFIT_BUILD_ROLE_IMAGE_PACKAGE ||
             role == CONFIT_BUILD_ROLE_IMAGE_SEAL ||
             role == CONFIT_BUILD_ROLE_TEST_ROOT;
    default:
      return 0;
  }
}

static int confit_build_output_role_allowed(ConfitBuildDomain domain,
                                            ConfitBuildRole role) {
  switch (domain) {
    case CONFIT_BUILD_DOMAIN_TOOLGEN:
      return role == CONFIT_BUILD_ROLE_TOOL_EXECUTABLE ||
             role == CONFIT_BUILD_ROLE_TOOL_SEAL ||
             role == CONFIT_BUILD_ROLE_TOOL_ROOT;
    case CONFIT_BUILD_DOMAIN_ENVGEN:
      return role == CONFIT_BUILD_ROLE_ENV_HEADER ||
             role == CONFIT_BUILD_ROLE_ENV_CRT ||
             role == CONFIT_BUILD_ROLE_ENV_LIBRARY ||
             role == CONFIT_BUILD_ROLE_ENV_SYSROOT_SEAL ||
             role == CONFIT_BUILD_ROLE_ENV_ROOT;
    case CONFIT_BUILD_DOMAIN_KERNGEN:
      return role == CONFIT_BUILD_ROLE_KERNEL_OBJECT ||
             role == CONFIT_BUILD_ROLE_KERNEL_ELF ||
             role == CONFIT_BUILD_ROLE_KERNEL_METADATA ||
             role == CONFIT_BUILD_ROLE_KERNEL_SEAL ||
             role == CONFIT_BUILD_ROLE_KERNEL_ROOT;
    case CONFIT_BUILD_DOMAIN_WORLDGEN:
      return role == CONFIT_BUILD_ROLE_WORLD_OBJECT ||
             role == CONFIT_BUILD_ROLE_WORLD_ARTIFACT ||
             role == CONFIT_BUILD_ROLE_WORLD_INSTALL_PLAN ||
             role == CONFIT_BUILD_ROLE_WORLD_SEAL ||
             role == CONFIT_BUILD_ROLE_WORLD_ROOT;
    case CONFIT_BUILD_DOMAIN_IMAGEGEN:
      return role == CONFIT_BUILD_ROLE_IMAGE_ARTIFACT ||
             role == CONFIT_BUILD_ROLE_IMAGE_PACKAGE ||
             role == CONFIT_BUILD_ROLE_IMAGE_SEAL ||
             role == CONFIT_BUILD_ROLE_IMAGE_ROOT;
    case CONFIT_BUILD_DOMAIN_TEST:
      return role == CONFIT_BUILD_ROLE_TEST_IMAGE ||
             role == CONFIT_BUILD_ROLE_TEST_LOG ||
             role == CONFIT_BUILD_ROLE_TEST_RECEIPT ||
             role == CONFIT_BUILD_ROLE_TEST_ROOT;
    default:
      return 0;
  }
}

static int confit_build_tool_allowed(ConfitBuildDomain domain,
                                     ConfitBuildActionKind kind,
                                     ConfitBuildToolRole tool) {
  if (kind == CONFIT_BUILD_ACTION_KIND_MKDIR ||
      kind == CONFIT_BUILD_ACTION_KIND_CLEAN ||
      kind == CONFIT_BUILD_ACTION_KIND_VERIFY ||
      kind == CONFIT_BUILD_ACTION_KIND_PUBLISH ||
      kind == CONFIT_BUILD_ACTION_KIND_COPY ||
      kind == CONFIT_BUILD_ACTION_KIND_PACKAGE) {
    return tool == CONFIT_BUILD_TOOL_BUILTIN;
  }
  if (domain == CONFIT_BUILD_DOMAIN_TOOLGEN) {
    return (kind == CONFIT_BUILD_ACTION_KIND_COMPILE ||
            kind == CONFIT_BUILD_ACTION_KIND_LINK)
               ? tool == CONFIT_BUILD_TOOL_HOST_CC
               : tool == CONFIT_BUILD_TOOL_BUILTIN;
  }
  if (domain == CONFIT_BUILD_DOMAIN_ENVGEN) {
    if (kind == CONFIT_BUILD_ACTION_KIND_COMPILE)
      return tool == CONFIT_BUILD_TOOL_TARGET_CC;
    if (kind == CONFIT_BUILD_ACTION_KIND_ARCHIVE)
      return tool == CONFIT_BUILD_TOOL_TARGET_AR;
    return tool == CONFIT_BUILD_TOOL_BUILTIN;
  }
  if (domain == CONFIT_BUILD_DOMAIN_KERNGEN) {
    if (kind == CONFIT_BUILD_ACTION_KIND_COMPILE)
      return tool == CONFIT_BUILD_TOOL_TARGET_CC;
    if (kind == CONFIT_BUILD_ACTION_KIND_LINK)
      return tool == CONFIT_BUILD_TOOL_TARGET_LD;
    if (kind == CONFIT_BUILD_ACTION_KIND_GENERATE)
      return tool == CONFIT_BUILD_TOOL_BUILTIN ||
             tool == CONFIT_BUILD_TOOL_DTC;
    return tool == CONFIT_BUILD_TOOL_BUILTIN;
  }
  if (domain == CONFIT_BUILD_DOMAIN_WORLDGEN) {
    if (kind == CONFIT_BUILD_ACTION_KIND_COMPILE)
      return tool == CONFIT_BUILD_TOOL_TARGET_CC;
    if (kind == CONFIT_BUILD_ACTION_KIND_ARCHIVE)
      return tool == CONFIT_BUILD_TOOL_TARGET_AR;
    if (kind == CONFIT_BUILD_ACTION_KIND_LINK)
      return tool == CONFIT_BUILD_TOOL_TARGET_LD;
    return tool == CONFIT_BUILD_TOOL_BUILTIN;
  }
  if (domain == CONFIT_BUILD_DOMAIN_IMAGEGEN) {
    return tool == CONFIT_BUILD_TOOL_BUILTIN;
  }
  /* Host unit, security, package와 docs lane도 current receipt를 쓰는 typed
   * test action이다. QEMU만 허용하면 이들 lane이 production action graph를
   * 우회해야 하므로 reviewed builtin test supervisor도 같은 closed kind에서
   * 허용한다. */
  if (domain == CONFIT_BUILD_DOMAIN_TEST) {
    if (kind == CONFIT_BUILD_ACTION_KIND_COMPILE ||
        kind == CONFIT_BUILD_ACTION_KIND_LINK) {
      return tool == CONFIT_BUILD_TOOL_HOST_CC;
    }
    return tool == CONFIT_BUILD_TOOL_BUILTIN ||
           tool == CONFIT_BUILD_TOOL_QEMU;
  }
  return 0;
}

static int confit_build_endpoint_counts_allowed(
    ConfitBuildActionKind kind, size_t input_count, size_t output_count) {
  if (kind == CONFIT_BUILD_ACTION_KIND_MKDIR)
    return input_count == 0U && output_count == 1U;
  if (kind == CONFIT_BUILD_ACTION_KIND_CLEAN)
    return input_count == 1U && output_count == 0U;
  return input_count != 0U && output_count != 0U;
}

static ConfitStatus confit_build_policy_ref_validate(
    const ConfitBuildPolicyRef *policy, ConfitDiagnostic *diagnostic) {
  size_t index;
  size_t prior;
  if (policy == 0 || policy->abi_version != CONFIT_BUILD_POLICY_ABI ||
      !confit_build_atom_valid(policy->profile_id) ||
      !confit_build_atom_valid(policy->target_id) ||
      !confit_build_sha256_valid(policy->configuration_sha256) ||
      !confit_build_sha256_valid(policy->target_sha256) ||
      !confit_build_sha256_valid(policy->policy_sha256) ||
      !confit_build_sha256_valid(policy->toolchain_plan_sha256) ||
      policy->edge_table_id == 0 ||
      strcmp(policy->edge_table_id, "parus-five-gen-edge-v1") != 0 ||
      policy->trusted_tools == 0 || policy->trusted_tool_count == 0U ||
      policy->trusted_tool_count > CONFIT_BUILD_ACTION_MAX_TRUSTED_TOOLS) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, "build-policy", 0U,
                          0U, "build policy reference is invalid or unsupported");
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < policy->trusted_tool_count; ++index) {
    const ConfitBuildActionTool *tool = &policy->trusted_tools[index];
    if (!confit_build_tool_role_valid(tool->role) ||
        !confit_build_absolute_path_valid(tool->path) ||
        !confit_build_sha256_valid(tool->sha256) ||
        !confit_build_atom_valid(tool->version)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, "build-policy.tool",
                            0U, 0U, "trusted tool identity is invalid");
      return CONFIT_ERR_SCHEMA;
    }
    for (prior = 0U; prior < index; ++prior) {
      const ConfitBuildActionTool *candidate = &policy->trusted_tools[prior];
      if (tool->role == candidate->role) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                              "build-policy.tool", 0U, 0U,
                              "trusted tool role has more than one identity");
        return CONFIT_ERR_SCHEMA;
      }
    }
  }
  return CONFIT_OK;
}

static int confit_build_tool_is_trusted(
    const ConfitBuildPolicyRef *policy, const ConfitBuildActionTool *tool) {
  size_t index;
  for (index = 0U; index < policy->trusted_tool_count; ++index) {
    const ConfitBuildActionTool *trusted = &policy->trusted_tools[index];
    if (trusted->role == tool->role && strcmp(trusted->path, tool->path) == 0 &&
        strcmp(trusted->sha256, tool->sha256) == 0 &&
        strcmp(trusted->version, tool->version) == 0) return 1;
  }
  return 0;
}

static int confit_build_wire_tool_is_trusted(
    const ConfitBuildPolicyRef *policy, ConfitBuildToolRole role,
    const ConfitBuildWireSpan *path, const ConfitBuildWireSpan *digest,
    const ConfitBuildWireSpan *version) {
  size_t index;
  for (index = 0U; index < policy->trusted_tool_count; ++index) {
    const ConfitBuildActionTool *trusted = &policy->trusted_tools[index];
    if (trusted->role == role && confit_build_span_equal_text(path, trusted->path) &&
        confit_build_span_equal_text(digest, trusted->sha256) &&
        confit_build_span_equal_text(version, trusted->version)) return 1;
  }
  return 0;
}

ConfitStatus confit_build_action_validate(
    const ConfitBuildPolicyRef *policy, const ConfitBuildAction *action,
    ConfitDiagnostic *diagnostic) {
  uint64_t maximum_sum = 0U;
  size_t index;
  size_t prior;
  ConfitStatus status = confit_build_policy_ref_validate(policy, diagnostic);
  if (status != CONFIT_OK) return status;
  if (action == 0 || !confit_build_domain_valid(action->domain) ||
      !confit_build_kind_valid(action->kind) ||
      !confit_build_kind_allowed(action->domain, action->kind) ||
      !confit_build_atom_valid(action->source_owner) ||
      action->configuration_sha256 == 0 || action->target_sha256 == 0 ||
      action->policy_sha256 == 0 ||
      strcmp(action->configuration_sha256,
             policy->configuration_sha256) != 0 ||
      strcmp(action->target_sha256, policy->target_sha256) != 0 ||
      strcmp(action->policy_sha256, policy->policy_sha256) != 0 ||
      !confit_build_tool_role_valid(action->tool.role) ||
      !confit_build_tool_allowed(action->domain, action->kind,
                                 action->tool.role) ||
      !confit_build_absolute_path_valid(action->tool.path) ||
      !confit_build_sha256_valid(action->tool.sha256) ||
      !confit_build_atom_valid(action->tool.version) ||
      !confit_build_tool_is_trusted(policy, &action->tool) ||
      action->input_count > CONFIT_BUILD_ACTION_MAX_INPUTS ||
      action->output_count > CONFIT_BUILD_ACTION_MAX_OUTPUTS ||
      !confit_build_endpoint_counts_allowed(action->kind, action->input_count,
                                            action->output_count) ||
      (action->input_count != 0U && action->inputs == 0) ||
      (action->output_count != 0U && action->outputs == 0) ||
      action->action_quota_bytes == 0U ||
      action->action_quota_bytes > CONFIT_BUILD_ACTION_MAX_QUOTA_BYTES) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, "build-action", 0U,
                          0U, "build action identity, authority or bound is invalid");
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < action->input_count; ++index) {
    const ConfitBuildActionInput *input = &action->inputs[index];
    if (!confit_build_role_valid(input->role) ||
        !confit_build_input_role_allowed(action->domain, input->role) ||
        !confit_build_relative_path_valid(input->path) ||
        !confit_build_sha256_valid(input->sha256) ||
        !confit_build_atom_valid(input->owner)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, "build-action.input",
                            0U, 0U, "build action input is invalid or forbidden");
      return CONFIT_ERR_SCHEMA;
    }
    for (prior = 0U; prior < index; ++prior) {
      if (strcmp(input->path, action->inputs[prior].path) == 0) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                              "build-action.input", 0U, 0U,
                              "build action contains a duplicate input");
        return CONFIT_ERR_SCHEMA;
      }
    }
  }
  for (index = 0U; index < action->output_count; ++index) {
    const ConfitBuildActionOutput *output = &action->outputs[index];
    if (!confit_build_role_valid(output->role) ||
        !confit_build_output_role_allowed(action->domain, output->role) ||
        !confit_build_relative_path_valid(output->path) ||
        output->maximum_bytes == 0U ||
        output->maximum_bytes > action->action_quota_bytes ||
        UINT64_MAX - maximum_sum < output->maximum_bytes) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                            "build-action.output", 0U, 0U,
                            "build action output is invalid or over quota");
      return CONFIT_ERR_SCHEMA;
    }
    maximum_sum += output->maximum_bytes;
    for (prior = 0U; prior < index; ++prior) {
      if (strcmp(output->path, action->outputs[prior].path) == 0) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                              "build-action.output", 0U, 0U,
                              "build action contains a duplicate output");
        return CONFIT_ERR_SCHEMA;
      }
    }
  }
  if (maximum_sum > action->action_quota_bytes) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                          "build-action.quota", 0U, 0U,
                          "declared outputs exceed the action quota");
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static int confit_build_wire_write_u8(ConfitBuildWireWriter *writer,
                                      unsigned value) {
  if (writer->cursor >= writer->capacity || value > 255U) return 0;
  writer->bytes[writer->cursor++] = (unsigned char)value;
  return 1;
}

static int confit_build_wire_write_u16(ConfitBuildWireWriter *writer,
                                       uint16_t value) {
  return confit_build_wire_write_u8(writer, value & 0xffU) &&
         confit_build_wire_write_u8(writer, (value >> 8U) & 0xffU);
}

static int confit_build_wire_write_u32(ConfitBuildWireWriter *writer,
                                       uint32_t value) {
  return confit_build_wire_write_u16(writer, (uint16_t)(value & 0xffffU)) &&
         confit_build_wire_write_u16(writer, (uint16_t)(value >> 16U));
}

static int confit_build_wire_write_u64(ConfitBuildWireWriter *writer,
                                       uint64_t value) {
  return confit_build_wire_write_u32(writer, (uint32_t)(value & 0xffffffffU)) &&
         confit_build_wire_write_u32(writer, (uint32_t)(value >> 32U));
}

static int confit_build_wire_write_raw(ConfitBuildWireWriter *writer,
                                       const void *bytes, size_t size) {
  if (size > writer->capacity - writer->cursor) return 0;
  memcpy(writer->bytes + writer->cursor, bytes, size);
  writer->cursor += size;
  return 1;
}

static int confit_build_wire_write_string(ConfitBuildWireWriter *writer,
                                          const char *text) {
  const size_t size = strlen(text);
  return size <= UINT16_MAX &&
         confit_build_wire_write_u16(writer, (uint16_t)size) &&
         confit_build_wire_write_raw(writer, text, size);
}

ConfitStatus confit_build_action_wire_encode(
    const ConfitBuildPolicyRef *policy, const ConfitBuildAction *action,
    unsigned char *out_bytes, size_t capacity, size_t *out_size,
    char out_action_id[65], ConfitDiagnostic *diagnostic) {
  static const unsigned char magic[8] = {'P','B','A','C','T','N','0','1'};
  static const char placeholder[64] = {
      '0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0',
      '0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0',
      '0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0',
      '0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0'};
  ConfitBuildWireWriter writer;
  size_t index;
  ConfitStatus status;
  if (out_size != 0) *out_size = 0U;
  if (out_action_id != 0) out_action_id[0] = '\0';
  if (out_bytes == 0 || out_size == 0 || out_action_id == 0 ||
      capacity < CONFIT_BUILD_ACTION_HEADER_SIZE +
                     CONFIT_BUILD_ACTION_ID_SIZE ||
      capacity > CONFIT_BUILD_ACTION_MAX_BYTES) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(out_bytes, 0, capacity);
  status = confit_build_action_validate(policy, action, diagnostic);
  if (status != CONFIT_OK) return status;
  writer.bytes = out_bytes;
  writer.capacity = capacity;
  writer.cursor = 0U;
  if (!confit_build_wire_write_raw(&writer, magic, sizeof(magic)) ||
      !confit_build_wire_write_u16(&writer,
                                   CONFIT_BUILD_ACTION_WIRE_VERSION) ||
      !confit_build_wire_write_u16(&writer,
                                   CONFIT_BUILD_ACTION_HEADER_SIZE) ||
      !confit_build_wire_write_u32(&writer, 0U) ||
      !confit_build_wire_write_u8(&writer, 1U) ||
      !confit_build_wire_write_u8(&writer, (unsigned)action->domain) ||
      !confit_build_wire_write_u8(&writer, 0U) ||
      !confit_build_wire_write_u8(&writer, 1U) ||
      !confit_build_wire_write_u8(&writer, (unsigned)action->kind) ||
      !confit_build_wire_write_u8(&writer, 0U) ||
      !confit_build_wire_write_u8(&writer, 1U) ||
      !confit_build_wire_write_u8(&writer,
                                  CONFIT_BUILD_ACTION_NOFOLLOW_FLAG) ||
      !confit_build_wire_write_u16(&writer, (uint16_t)action->input_count) ||
      !confit_build_wire_write_u16(&writer, (uint16_t)action->output_count) ||
      !confit_build_wire_write_u32(&writer, 0U) ||
      writer.cursor != CONFIT_BUILD_ACTION_HEADER_SIZE ||
      !confit_build_wire_write_raw(&writer, placeholder, sizeof(placeholder)) ||
      !confit_build_wire_write_string(&writer, action->source_owner) ||
      !confit_build_wire_write_raw(&writer, action->configuration_sha256, 64U) ||
      !confit_build_wire_write_raw(&writer, action->target_sha256, 64U) ||
      !confit_build_wire_write_raw(&writer, action->policy_sha256, 64U) ||
      !confit_build_wire_write_u16(&writer, (uint16_t)action->tool.role) ||
      !confit_build_wire_write_string(&writer, action->tool.path) ||
      !confit_build_wire_write_raw(&writer, action->tool.sha256, 64U) ||
      !confit_build_wire_write_string(&writer, action->tool.version) ||
      !confit_build_wire_write_u64(&writer, action->action_quota_bytes)) {
    status = CONFIT_ERR_SCHEMA;
    goto failed;
  }
  for (index = 0U; index < action->input_count; ++index) {
    const ConfitBuildActionInput *input = &action->inputs[index];
    if (!confit_build_wire_write_u16(&writer, (uint16_t)input->role) ||
        !confit_build_wire_write_string(&writer, input->path) ||
        !confit_build_wire_write_raw(&writer, input->sha256, 64U) ||
        !confit_build_wire_write_string(&writer, input->owner)) {
      status = CONFIT_ERR_SCHEMA;
      goto failed;
    }
  }
  for (index = 0U; index < action->output_count; ++index) {
    const ConfitBuildActionOutput *output = &action->outputs[index];
    if (!confit_build_wire_write_u16(&writer, (uint16_t)output->role) ||
        !confit_build_wire_write_string(&writer, output->path) ||
        !confit_build_wire_write_u64(&writer, output->maximum_bytes)) {
      status = CONFIT_ERR_SCHEMA;
      goto failed;
    }
  }
  if (writer.cursor > UINT32_MAX || writer.cursor > CONFIT_BUILD_ACTION_MAX_BYTES) {
    status = CONFIT_ERR_SCHEMA;
    goto failed;
  }
  out_bytes[12] = (unsigned char)(writer.cursor & 0xffU);
  out_bytes[13] = (unsigned char)((writer.cursor >> 8U) & 0xffU);
  out_bytes[14] = (unsigned char)((writer.cursor >> 16U) & 0xffU);
  out_bytes[15] = (unsigned char)((writer.cursor >> 24U) & 0xffU);
  confit_v4_sha256_bytes(out_bytes, writer.cursor, out_action_id);
  memcpy(out_bytes + CONFIT_BUILD_ACTION_HEADER_SIZE, out_action_id,
         CONFIT_BUILD_ACTION_ID_SIZE);
  *out_size = writer.cursor;
  return CONFIT_OK;

failed:
  memset(out_bytes, 0, capacity);
  *out_size = 0U;
  out_action_id[0] = '\0';
  confit_diagnostic_set(diagnostic, status, "build-action.wire", 0U, 0U,
                        "build action exceeds its bounded wire form");
  return status;
}

static int confit_build_wire_read_u8(ConfitBuildWireReader *reader,
                                     unsigned *out) {
  if (reader->cursor >= reader->size) return 0;
  *out = reader->bytes[reader->cursor++];
  return 1;
}

static int confit_build_wire_read_u16(ConfitBuildWireReader *reader,
                                      uint16_t *out) {
  unsigned low;
  unsigned high;
  if (!confit_build_wire_read_u8(reader, &low) ||
      !confit_build_wire_read_u8(reader, &high)) return 0;
  *out = (uint16_t)(low | (high << 8U));
  return 1;
}

static int confit_build_wire_read_u32(ConfitBuildWireReader *reader,
                                      uint32_t *out) {
  uint16_t low;
  uint16_t high;
  if (!confit_build_wire_read_u16(reader, &low) ||
      !confit_build_wire_read_u16(reader, &high)) return 0;
  *out = (uint32_t)low | ((uint32_t)high << 16U);
  return 1;
}

static int confit_build_wire_read_u64(ConfitBuildWireReader *reader,
                                      uint64_t *out) {
  uint32_t low;
  uint32_t high;
  if (!confit_build_wire_read_u32(reader, &low) ||
      !confit_build_wire_read_u32(reader, &high)) return 0;
  *out = (uint64_t)low | ((uint64_t)high << 32U);
  return 1;
}

static int confit_build_wire_read_span(ConfitBuildWireReader *reader,
                                       size_t size,
                                       ConfitBuildWireSpan *out) {
  if (size > reader->size - reader->cursor) return 0;
  out->bytes = reader->bytes + reader->cursor;
  out->size = size;
  reader->cursor += size;
  return 1;
}

static int confit_build_wire_read_string(ConfitBuildWireReader *reader,
                                         ConfitBuildWireSpan *out) {
  uint16_t size;
  return confit_build_wire_read_u16(reader, &size) && size != 0U &&
         confit_build_wire_read_span(reader, size, out);
}

static int confit_build_span_equal_text(const ConfitBuildWireSpan *span,
                                        const char *text) {
  return text != 0 && span->size == strlen(text) &&
         memcmp(span->bytes, text, span->size) == 0;
}

static int confit_build_span_copy(const ConfitBuildWireSpan *span, char *out,
                                  size_t capacity) {
  if (span->size + 1U > capacity) return 0;
  memcpy(out, span->bytes, span->size);
  out[span->size] = '\0';
  return 1;
}

ConfitStatus confit_build_action_wire_validate(
    const ConfitBuildPolicyRef *policy, const unsigned char *bytes,
    size_t size, ConfitDiagnostic *diagnostic) {
  static const unsigned char magic[8] = {'P','B','A','C','T','N','0','1'};
  ConfitBuildWireReader reader;
  ConfitBuildWireSpan action_id;
  ConfitBuildWireSpan source_owner;
  ConfitBuildWireSpan configuration;
  ConfitBuildWireSpan target;
  ConfitBuildWireSpan policy_digest;
  ConfitBuildWireSpan tool_path;
  ConfitBuildWireSpan tool_digest;
  ConfitBuildWireSpan tool_version;
  ConfitBuildWireEndpoint inputs[CONFIT_BUILD_ACTION_MAX_INPUTS];
  ConfitBuildWireEndpoint outputs[CONFIT_BUILD_ACTION_MAX_OUTPUTS];
  char text[CONFIT_BUILD_ACTION_MAX_PATH_BYTES + 1U];
  unsigned char canonical[CONFIT_BUILD_ACTION_MAX_BYTES];
  char expected_id[65];
  unsigned domain_count;
  unsigned domain_value;
  unsigned second_domain;
  unsigned kind_count;
  unsigned kind_value;
  unsigned second_kind;
  unsigned owner_count;
  unsigned flags;
  uint16_t version;
  uint16_t header_size;
  uint16_t input_count;
  uint16_t output_count;
  uint16_t tool_role_value;
  uint32_t total_size;
  uint32_t reserved;
  uint64_t action_quota;
  uint64_t maximum_sum = 0U;
  size_t index;
  size_t prior;
  ConfitBuildDomain domain;
  ConfitBuildActionKind kind;
  ConfitBuildToolRole tool_role;
  ConfitStatus status = confit_build_policy_ref_validate(policy, diagnostic);
  if (status != CONFIT_OK) return status;
  if (bytes == 0 || size < CONFIT_BUILD_ACTION_HEADER_SIZE +
                                  CONFIT_BUILD_ACTION_ID_SIZE ||
      size > CONFIT_BUILD_ACTION_MAX_BYTES ||
      memcmp(bytes, magic, sizeof(magic)) != 0) goto invalid;
  reader.bytes = bytes;
  reader.size = size;
  reader.cursor = sizeof(magic);
  if (!confit_build_wire_read_u16(&reader, &version) ||
      !confit_build_wire_read_u16(&reader, &header_size) ||
      !confit_build_wire_read_u32(&reader, &total_size) ||
      !confit_build_wire_read_u8(&reader, &domain_count) ||
      !confit_build_wire_read_u8(&reader, &domain_value) ||
      !confit_build_wire_read_u8(&reader, &second_domain) ||
      !confit_build_wire_read_u8(&reader, &kind_count) ||
      !confit_build_wire_read_u8(&reader, &kind_value) ||
      !confit_build_wire_read_u8(&reader, &second_kind) ||
      !confit_build_wire_read_u8(&reader, &owner_count) ||
      !confit_build_wire_read_u8(&reader, &flags) ||
      !confit_build_wire_read_u16(&reader, &input_count) ||
      !confit_build_wire_read_u16(&reader, &output_count) ||
      !confit_build_wire_read_u32(&reader, &reserved) ||
      version != CONFIT_BUILD_ACTION_WIRE_VERSION ||
      header_size != CONFIT_BUILD_ACTION_HEADER_SIZE || total_size != size ||
      domain_count != 1U || second_domain != 0U || kind_count != 1U ||
      second_kind != 0U || owner_count != 1U ||
      flags != CONFIT_BUILD_ACTION_NOFOLLOW_FLAG || reserved != 0U ||
      input_count > CONFIT_BUILD_ACTION_MAX_INPUTS ||
      output_count > CONFIT_BUILD_ACTION_MAX_OUTPUTS ||
      !confit_build_endpoint_counts_allowed((ConfitBuildActionKind)kind_value,
                                            input_count, output_count) ||
      reader.cursor != CONFIT_BUILD_ACTION_HEADER_SIZE) goto invalid;
  domain = (ConfitBuildDomain)domain_value;
  kind = (ConfitBuildActionKind)kind_value;
  if (!confit_build_domain_valid(domain) || !confit_build_kind_valid(kind) ||
      !confit_build_kind_allowed(domain, kind) ||
      !confit_build_wire_read_span(&reader, CONFIT_BUILD_ACTION_ID_SIZE,
                                   &action_id) ||
      !confit_build_wire_read_string(&reader, &source_owner) ||
      !confit_build_wire_read_span(&reader, 64U, &configuration) ||
      !confit_build_wire_read_span(&reader, 64U, &target) ||
      !confit_build_wire_read_span(&reader, 64U, &policy_digest) ||
      !confit_build_wire_read_u16(&reader, &tool_role_value) ||
      !confit_build_wire_read_string(&reader, &tool_path) ||
      !confit_build_wire_read_span(&reader, 64U, &tool_digest) ||
      !confit_build_wire_read_string(&reader, &tool_version) ||
      !confit_build_wire_read_u64(&reader, &action_quota)) goto invalid;
  tool_role = (ConfitBuildToolRole)tool_role_value;
  if (!confit_build_span_copy(&source_owner, text, sizeof(text)) ||
      !confit_build_atom_valid(text) ||
      !confit_build_span_equal_text(&configuration,
                                    policy->configuration_sha256) ||
      !confit_build_span_equal_text(&target, policy->target_sha256) ||
      !confit_build_span_equal_text(&policy_digest, policy->policy_sha256) ||
      !confit_build_tool_role_valid(tool_role) ||
      !confit_build_tool_allowed(domain, kind, tool_role) ||
      !confit_build_span_copy(&tool_path, text, sizeof(text)) ||
      !confit_build_absolute_path_valid(text) ||
      !confit_build_span_copy(&tool_digest, text, sizeof(text)) ||
      !confit_build_sha256_valid(text) ||
      !confit_build_span_copy(&tool_version, text, sizeof(text)) ||
      !confit_build_atom_valid(text) ||
      !confit_build_wire_tool_is_trusted(policy, tool_role, &tool_path,
                                         &tool_digest, &tool_version) ||
      action_quota == 0U ||
      action_quota > CONFIT_BUILD_ACTION_MAX_QUOTA_BYTES) goto invalid;
  for (index = 0U; index < input_count; ++index) {
    uint16_t role;
    ConfitBuildWireSpan digest;
    ConfitBuildWireSpan owner;
    if (!confit_build_wire_read_u16(&reader, &role) ||
        !confit_build_wire_read_string(&reader, &inputs[index].path) ||
        !confit_build_wire_read_span(&reader, 64U, &digest) ||
        !confit_build_wire_read_string(&reader, &owner)) goto invalid;
    inputs[index].role = (ConfitBuildRole)role;
    if (!confit_build_role_valid(inputs[index].role) ||
        !confit_build_input_role_allowed(domain, inputs[index].role) ||
        !confit_build_span_copy(&inputs[index].path, text, sizeof(text)) ||
        !confit_build_relative_path_valid(text) ||
        !confit_build_span_copy(&digest, text, sizeof(text)) ||
        !confit_build_sha256_valid(text) ||
        !confit_build_span_copy(&owner, text, sizeof(text)) ||
        !confit_build_atom_valid(text)) goto invalid;
    for (prior = 0U; prior < index; ++prior) {
      if (inputs[index].path.size == inputs[prior].path.size &&
          memcmp(inputs[index].path.bytes, inputs[prior].path.bytes,
                 inputs[index].path.size) == 0) goto invalid;
    }
  }
  for (index = 0U; index < output_count; ++index) {
    uint16_t role;
    if (!confit_build_wire_read_u16(&reader, &role) ||
        !confit_build_wire_read_string(&reader, &outputs[index].path) ||
        !confit_build_wire_read_u64(&reader,
                                    &outputs[index].maximum_bytes)) goto invalid;
    outputs[index].role = (ConfitBuildRole)role;
    if (!confit_build_role_valid(outputs[index].role) ||
        !confit_build_output_role_allowed(domain, outputs[index].role) ||
        !confit_build_span_copy(&outputs[index].path, text, sizeof(text)) ||
        !confit_build_relative_path_valid(text) ||
        outputs[index].maximum_bytes == 0U ||
        outputs[index].maximum_bytes > action_quota ||
        UINT64_MAX - maximum_sum < outputs[index].maximum_bytes) goto invalid;
    maximum_sum += outputs[index].maximum_bytes;
    for (prior = 0U; prior < index; ++prior) {
      if (outputs[index].path.size == outputs[prior].path.size &&
          memcmp(outputs[index].path.bytes, outputs[prior].path.bytes,
                 outputs[index].path.size) == 0) goto invalid;
    }
  }
  if (reader.cursor != size || maximum_sum > action_quota) goto invalid;
  memcpy(canonical, bytes, size);
  memset(canonical + CONFIT_BUILD_ACTION_HEADER_SIZE, '0',
         CONFIT_BUILD_ACTION_ID_SIZE);
  confit_v4_sha256_bytes(canonical, size, expected_id);
  if (!confit_build_span_equal_text(&action_id, expected_id)) goto invalid;
  return CONFIT_OK;

invalid:
  confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, "build-action.wire",
                        0U, 0U,
                        "build action wire is malformed, stale or unauthorized");
  return CONFIT_ERR_SCHEMA;
}
