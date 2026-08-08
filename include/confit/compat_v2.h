#ifndef CONFIT_COMPAT_V2_H
#define CONFIT_COMPAT_V2_H

#include <stddef.h>
#include <stdint.h>

#include "confit/diagnostic.h"
#include "confit/resolver_v2.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief V2 compatibility source가 선언해야 하는 schema version이다. */
#define CONFIT_V2_COMPAT_SCHEMA_VERSION 2U

/** @brief V2 snapshot을 봉인해 내보내는 artifact ABI의 canonical identifier다. */
#define CONFIT_V2_COMPAT_ARTIFACT_ABI "confit-artifact-v3"

/** @brief cross-project assertion의 action이다. */
typedef enum ConfitV2CompatAction {
  /** `require` expression이 true여야 한다. */
  CONFIT_V2_COMPAT_ACTION_REQUIRE = 1,
  /** `forbid` expression이 false여야 한다. */
  CONFIT_V2_COMPAT_ACTION_FORBID,
} ConfitV2CompatAction;

/** @brief 하나의 V2 compatibility constraint 평가 결과다. */
typedef enum ConfitV2CompatResultState {
  /** `when`이 false라 assertion을 적용하지 않았다. */
  CONFIT_V2_COMPAT_RESULT_NOT_APPLICABLE = 1,
  /** assertion이 적용됐고 통과했다. */
  CONFIT_V2_COMPAT_RESULT_PASS,
  /** assertion이 적용됐고 실패했다. */
  CONFIT_V2_COMPAT_RESULT_FAIL,
} ConfitV2CompatResultState;

/** @brief V2 snapshot을 checker에 전달하는 immutable project view다. */
typedef struct ConfitV2CompatProject {
  /** compatibility TOML의 `[projects]` alias와 정확히 일치해야 한다. */
  const char *alias;
  /** fully-resolved immutable V2 snapshot. */
  const ConfitV2Snapshot *snapshot;
  /** 반드시 `CONFIT_V2_COMPAT_SCHEMA_VERSION`이어야 한다. */
  unsigned int schema_version;
  /** 반드시 `CONFIT_V2_COMPAT_ARTIFACT_ABI`여야 한다. */
  const char *artifact_abi;
  /** optional canonical config root. 지정하면 snapshot provenance와 일치해야 한다. */
  const char *expected_source_root;
  /** optional expected source semantic hash. 0이면 external precondition을 생략한다. */
  uint64_t expected_source_hash;
  /** optional expected immutable snapshot hash. 0이면 external precondition을 생략한다. */
  uint64_t expected_snapshot_hash;
} ConfitV2CompatProject;

/** @brief parsed V2 compatibility source의 opaque ownership root다. */
typedef struct ConfitV2CompatSuite ConfitV2CompatSuite;

/** @brief typed compatibility check의 owned deterministic report다. */
typedef struct ConfitV2CompatReport ConfitV2CompatReport;

/**
 * @brief 하나의 strict V2 compatibility TOML file을 load한다.
 *
 * Source는 `[compat]`, `[projects]`, `[[constraint]]`만 허용한다. constraint는
 * `id`, optional `when`, exactly-one `require`/`forbid`, `message`를 가진다.
 * 반환 suite는 caller가 `confit_v2_compat_suite_free()`로 해제한다.
 */
ConfitStatus confit_v2_compat_load_file(const char *path,
                                        ConfitV2CompatSuite **out_suite,
                                        ConfitDiagnostic *diagnostic);

/** @brief parsed V2 compatibility suite와 ownership tree를 해제한다. */
void confit_v2_compat_suite_free(ConfitV2CompatSuite *suite);

/**
 * @brief V2 snapshots만 대상으로 typed cross-project assertion을 평가한다.
 *
 * failed constraint도 report에 보존하며 이 경우 return status는
 * `CONFIT_ERR_COMPATIBILITY`이다. schema/ABI mismatch, alias mismatch,
 * expression type failure는 hard schema error이며 report를 만들지 않는다.
 */
ConfitStatus confit_v2_compat_check(
    const ConfitV2CompatSuite *suite, const ConfitV2CompatProject *projects,
    size_t project_count, ConfitV2CompatReport **out_report,
    ConfitDiagnostic *diagnostic);

/** @brief V2 compatibility report와 모든 copied causal value를 해제한다. */
void confit_v2_compat_report_free(ConfitV2CompatReport *report);

/** @brief report의 deterministic semantic hash를 반환한다. NULL이면 0이다. */
uint64_t confit_v2_compat_report_semantic_hash(const ConfitV2CompatReport *report);

/**
 * @brief report를 canonical JSON 문자열로 만든다.
 *
 * 반환 문자열은 caller-owned이며 `confit_v2_compat_string_free()`로 해제한다.
 */
ConfitStatus confit_v2_compat_report_to_json(const ConfitV2CompatReport *report,
                                              char **out_json);

/** @brief report를 사람이 읽는 deterministic text로 만든다. */
ConfitStatus confit_v2_compat_report_to_text(const ConfitV2CompatReport *report,
                                              char **out_text);

/** @brief V2 compatibility serializer가 반환한 문자열을 해제한다. */
void confit_v2_compat_string_free(char *text);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_COMPAT_V2_H */
