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

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_GENERATOR_V2_H */
