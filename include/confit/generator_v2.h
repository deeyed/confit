#ifndef CONFIT_GENERATOR_V2_H
#define CONFIT_GENERATOR_V2_H

#include <stddef.h>

#include "confit/constraint_v2.h"
#include "confit/diagnostic.h"
#include "confit/resolver_v2.h"
#include "confit/status.h"

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

/** @brief v2 artifact generator의 non-semantic input이다. */
typedef struct ConfitV2ArtifactOptions {
  /** static edge graph를 넣을 optional immutable compiled structure다. */
  const ConfitV2CompiledStructure *compiled;
  /** config.inputs.json에 기록할 caller-owned input records다. */
  const ConfitV2ArtifactInput *inputs;
  size_t input_count;
  /** selection emit module directory/name. 없으면 `build_selection`이다. */
  const char *selection_name;
} ConfitV2ArtifactOptions;

/** @brief single immutable snapshot에서 만들어진 owned v2 artifact bundle이다. */
typedef struct ConfitV2ArtifactSet {
  char *config_header;
  char *report_json;
  char *explain_text;
  char *graph_json;
  char *inputs_json;
  char *changes_json;
  char *cmake_fragment;
  char *qsm_module;
  char *selection_module;
  char *selection_name;
} ConfitV2ArtifactSet;

/** @brief sealed generation v3가 만들 수 있는 consumer format mask다. */
typedef enum ConfitV3ArtifactMask {
  /** C/ASM configuration header를 생성한다. */
  CONFIT_V3_ARTIFACT_HEADER = 1U << 0,
  /** machine-readable report와 input provenance를 생성한다. */
  CONFIT_V3_ARTIFACT_REPORTS = 1U << 1,
  /** typed semantic selection JSON을 생성한다. */
  CONFIT_V3_ARTIFACT_SELECTION = 1U << 2,
  /** restricted bmake adapter를 생성한다. */
  CONFIT_V3_ARTIFACT_MAKE_ADAPTER = 1U << 3,
  /** Parus configure가 요구하는 complete sealed set이다. */
  CONFIT_V3_ARTIFACT_COMPLETE =
      CONFIT_V3_ARTIFACT_HEADER | CONFIT_V3_ARTIFACT_REPORTS |
      CONFIT_V3_ARTIFACT_SELECTION | CONFIT_V3_ARTIFACT_MAKE_ADAPTER,
} ConfitV3ArtifactMask;

/** @brief v3 serializer의 caller-owned immutable input이다. */
typedef struct ConfitV3ArtifactOptions {
  /** complete provenance record; digest는 `sha256:<lowercase-hex>`여야 한다. */
  const ConfitV2ArtifactInput *inputs;
  size_t input_count;
  /** tool provenance string. NULL이면 Confit release identity를 사용한다. */
  const char *tool_identity;
  /** independent semantic output request다. 0은 complete set을 의미한다. */
  unsigned int artifact_mask;
} ConfitV3ArtifactOptions;

/** @brief one immutable snapshot에서 만든 owned ABI v3 semantic bundle이다. */
typedef struct ConfitV3ArtifactSet {
  char *config_header;
  char *selection_json;
  char *report_json;
  char *inputs_json;
  char *config_mk;
  char *config_values_mk;
  char *components_mk;
  char *component_catalog_json;
  char *bundle_json;
  /** lower-case SHA-256 digest without a `sha256:` prefix다. */
  char bundle_digest[65];
} ConfitV3ArtifactSet;

/** @brief sealed bundle publish request다. */
typedef struct ConfitV3PublishOptions {
  /** `generations/`와 `selected` alias를 둘 output root다. */
  const char *output_root;
  /** nonzero면 test가 해당 artifact write 뒤 failure를 주입한다. */
  size_t fault_after_artifact;
} ConfitV3PublishOptions;

/** @brief NUL-terminated text의 strong SHA-256을 lower-case hex로 쓴다. */
void confit_v3_sha256_hex(const char *text, char output[65]);

/**
 * @brief immutable v2 snapshot에서 모든 deterministic artifact text를 생성한다.
 *
 * 이 API는 resolver를 호출하거나 snapshot을 변경하지 않는다. 각 returned text는
 * LF로 끝나며 timestamp와 absolute source path를 넣지 않는다.
 */
ConfitStatus confit_v2_generate_artifacts(
    const ConfitV2Snapshot *snapshot, const ConfitV2ArtifactOptions *options,
    ConfitV2ArtifactSet *out_artifacts, ConfitDiagnostic *diagnostic);

/** @brief artifact bundle이 소유한 모든 string을 해제한다. */
void confit_v2_artifact_set_clear(ConfitV2ArtifactSet *artifacts);

/**
 * @brief bundle을 output root에 write-if-changed atomic replacement로 publish한다.
 *
 * 모든 artifact text는 write 시작 전에 이미 생성되어 있어 partial serialization은
 * 없다. each file은 same-directory temporary file과 atomic replace를 사용한다.
 * `out_changed_file_count`는 실제 bytes가 교체된 파일 수다.
 */
ConfitStatus confit_v2_write_artifacts(
    const char *output_root, const ConfitV2ArtifactSet *artifacts,
    size_t *out_changed_file_count, ConfitDiagnostic *diagnostic);

/**
 * @brief immutable v2 snapshot을 backend-neutral ABI v3 artifact set으로 만든다.
 *
 * 이 함수는 selection의 의미를 바꾸지 않는다. Make adapter는 closed safe atom
 * vocabulary에 맞지 않는 emitted value를 escape하지 않고 adapter에서 제외한다.
 */
ConfitStatus confit_v3_generate_artifacts(
    const ConfitV2Snapshot *snapshot, const ConfitV3ArtifactOptions *options,
    ConfitV3ArtifactSet *out_artifacts, ConfitDiagnostic *diagnostic);

/** @brief v3 artifact bundle이 소유한 strings를 해제한다. */
void confit_v3_artifact_set_clear(ConfitV3ArtifactSet *artifacts);

/**
 * @brief complete v3 bundle을 staging 후 exact digest generation으로 atomic publish한다.
 *
 * Published generation은 `output_root/generations/<bundle-digest>`에만 생기며,
 * `selected` alias는 publication 뒤 마지막으로 바뀐다.
 */
ConfitStatus confit_v3_publish_artifacts(
    const ConfitV3PublishOptions *options,
    const ConfitV3ArtifactSet *artifacts, size_t *out_changed_file_count,
    ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_GENERATOR_V2_H */
