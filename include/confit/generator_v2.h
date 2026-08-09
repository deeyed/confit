#ifndef CONFIT_GENERATOR_V2_H
#define CONFIT_GENERATOR_V2_H

#include <stddef.h>

#include "confit/component_catalog.h"
#include "confit/constraint_v2.h"
#include "confit/diagnostic.h"
#include "confit/resolver_v2.h"
#include "confit/status.h"
#include "confit/target_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief v2 artifact input manifest의 one file record다. */
typedef struct ConfitV2ArtifactInput {
  /** config root 기준 canonical logical path다. */
  const char *path;
  /** deterministic content digest다. */
  const char *content_hash;
  /** `schema`, `profile`, `target`, `override` 같은 input role이다. */
  const char *role;
} ConfitV2ArtifactInput;

/** @brief v4 serializer의 caller-owned immutable input이다. */
typedef struct ConfitV4ArtifactOptions {
  /** complete provenance record; digest는 `sha256:<lowercase-hex>`여야 한다. */
  const ConfitV2ArtifactInput *inputs;
  size_t input_count;
  /** tool provenance string. NULL이면 Confit release identity를 사용한다. */
  const char *tool_identity;
  /** configured catalog availability. NULL이면 catalog ABI를 publish하지 않는다. */
  const ConfitComponentCatalog *component_catalog;
  /** catalog에서 resolved한 exact root closure. catalog와 함께만 유효하다. */
  const ConfitComponentClosure *component_closure;
  /** selected target/toolchain의 closed build tuple이다. */
  const ConfitTargetPlan *target_plan;
} ConfitV4ArtifactOptions;

/** @brief one immutable snapshot에서 만든 owned ABI v4 semantic bundle이다. */
typedef struct ConfitV4ArtifactSet {
  char *config_header;
  char *selection_json;
  char *reason_json;
  char *report_json;
  char *inputs_json;
  char *config_mk;
  char *config_values_mk;
  char *components_mk;
  char *target_mk;
  char *tests_mk;
  char *component_catalog_json;
  char *bundle_json;
  /** lower-case SHA-256 digest without a `sha256:` prefix다. */
  char bundle_digest[65];
} ConfitV4ArtifactSet;

/** @brief sealed bundle publish request다. */
typedef struct ConfitV4PublishOptions {
  /** `generations/`와 atomic `selected` directory alias를 둘 output root다. */
  const char *output_root;
  /** nonzero면 test가 해당 artifact write 뒤 failure를 주입한다. */
  size_t fault_after_artifact;
} ConfitV4PublishOptions;

/** @brief NUL-terminated text의 strong SHA-256을 lower-case hex로 쓴다. */
void confit_v4_sha256_hex(const char *text, char output[65]);

/** @brief 최대 256 MiB regular file을 읽어 lower-case SHA-256 identity를 만든다. */
ConfitStatus confit_v4_sha256_file(const char *path, char output[65],
                                   ConfitDiagnostic *diagnostic);

/**
 * @brief immutable v2 snapshot을 complete sealed ABI v4 artifact set으로 만든다.
 *
 * 이 함수는 partial output selector를 제공하지 않는다. Make adapter는 closed safe
 * atom vocabulary에 맞지 않는 emitted value를 escape하지 않고 adapter에서 제외한다.
 */
ConfitStatus confit_v4_generate_artifacts(
    const ConfitV2Snapshot *snapshot, const ConfitV4ArtifactOptions *options,
    ConfitV4ArtifactSet *out_artifacts, ConfitDiagnostic *diagnostic);

/** @brief v4 artifact bundle이 소유한 strings를 해제한다. */
void confit_v4_artifact_set_clear(ConfitV4ArtifactSet *artifacts);

/**
 * @brief complete v4 bundle을 staging 후 exact digest generation으로 atomic publish한다.
 *
 * Published generation은 `output_root/generations/<bundle-digest>`에만 생기며,
 * `selected`는 exact generation을 가리키는 relative directory alias이며 complete
 * publication 뒤 마지막으로 atomic 교체된다.
 */
ConfitStatus confit_v4_publish_artifacts(
    const ConfitV4PublishOptions *options,
    const ConfitV4ArtifactSet *artifacts, size_t *out_changed_file_count,
    ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_GENERATOR_V2_H */
