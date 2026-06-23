#ifndef CONFIT_COMPAT_H
#define CONFIT_COMPAT_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/model.h"
#include "confit/resolver.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief compatibility assertion action kind다.
 */
typedef enum ConfitCompatActionKind {
  /** `requires` 조건이다. */
  CONFIT_COMPAT_REQUIRES = 1,
  /** `forbids` 조건이다. */
  CONFIT_COMPAT_FORBIDS = 2,
} ConfitCompatActionKind;

/**
 * @brief compatibility assertion result kind다.
 */
typedef enum ConfitCompatResultStatus {
  /** `when` 조건이 match되지 않아 assertion을 적용하지 않았다. */
  CONFIT_COMPAT_RESULT_SKIPPED = 1,
  /** assertion이 적용되었고 통과했다. */
  CONFIT_COMPAT_RESULT_PASS = 2,
  /** assertion이 적용되었고 실패했다. */
  CONFIT_COMPAT_RESULT_FAIL = 3,
} ConfitCompatResultStatus;

/**
 * @brief compatibility inline condition이다.
 *
 * `option_id`와 `equals` payload는 condition이 소유한다.
 */
typedef struct ConfitCompatCondition {
  /** global option id. */
  char *option_id;
  /** 기대 value. */
  ConfitValue equals;
} ConfitCompatCondition;

/**
 * @brief 하나의 compatibility assertion이다.
 *
 * `when`이 match될 때 `requires`는 action 조건이 match되어야 하고,
 * `forbids`는 action 조건이 match되면 실패한다.
 */
typedef struct ConfitCompatAssertion {
  /** assertion trigger condition. */
  ConfitCompatCondition when;
  /** action kind. */
  ConfitCompatActionKind action;
  /** required 또는 forbidden condition. */
  ConfitCompatCondition condition;
  /** 사람이 읽는 failure message. */
  char *message;
  /** source provenance label. */
  char *source;
} ConfitCompatAssertion;

/**
 * @brief compat TOML 파일들에서 로드한 assertion 묶음이다.
 */
typedef struct ConfitCompatSuite {
  /** compatibility suite name. */
  char *name;
  /** assertion 목록. */
  ConfitCompatAssertion *assertions;
  /** assertion 개수. */
  size_t assertion_count;
} ConfitCompatSuite;

/**
 * @brief resolved project 하나를 compatibility checker에 넘기는 view다.
 */
typedef struct ConfitCompatProject {
  /** project model. */
  const ConfitProject *project;
  /** resolved config. */
  const ConfitResolvedConfig *config;
} ConfitCompatProject;

/**
 * @brief compatibility assertion 하나를 평가한 구조화 결과다.
 *
 * 문자열과 value payload는 result가 소유한다. `when_actual`과
 * `condition_actual`은 `has_*_actual`이 1일 때만 의미가 있다.
 */
typedef struct ConfitCompatResult {
  /** assertion 순서. */
  size_t assertion_index;
  /** 평가 결과. */
  ConfitCompatResultStatus status;
  /** action kind. */
  ConfitCompatActionKind action;
  /** trigger condition. */
  ConfitCompatCondition when;
  /** action condition. */
  ConfitCompatCondition condition;
  /** 사람이 읽는 결과 메시지. */
  char *message;
  /** source provenance label. */
  char *source;
  /** when option이 속한 project name. */
  char *when_project;
  /** action condition option이 속한 project name. */
  char *condition_project;
  /** when option의 실제 resolved value. */
  ConfitValue when_actual;
  /** action condition option의 실제 resolved value. */
  ConfitValue condition_actual;
  /** when 실제 값이 있으면 1. */
  int has_when_actual;
  /** action condition 실제 값이 있으면 1. */
  int has_condition_actual;
  /** when 조건이 match되었으면 1. */
  int when_matches;
  /** action condition 조건이 match되었으면 1. */
  int condition_matches;
} ConfitCompatResult;

/**
 * @brief compatibility suite 전체를 평가한 구조화 report다.
 */
typedef struct ConfitCompatReport {
  /** compatibility suite name. */
  char *suite_name;
  /** assertion별 결과 목록. */
  ConfitCompatResult *results;
  /** assertion별 결과 개수. */
  size_t result_count;
  /** pass 개수. */
  size_t pass_count;
  /** fail 개수. */
  size_t fail_count;
  /** skipped 개수. */
  size_t skipped_count;
} ConfitCompatReport;

/**
 * @brief compat TOML directory를 로드한다.
 *
 * Directory가 없으면 빈 suite를 성공으로 반환한다. 반환 suite는 caller가
 * 소유하며 `confit_compat_suite_free`로 해제한다.
 *
 * @param compat_dir compatibility TOML directory.
 * @param out_suite 성공 시 caller-owned suite를 받는다.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_compat_load_directory(const char *compat_dir,
                                          ConfitCompatSuite **out_suite,
                                          ConfitDiagnostic *diagnostic);

/**
 * @brief compatibility suite와 resolved project들을 검사하고 report를 만든다.
 *
 * 실패 assertion이 있어도 report는 생성된다. 이 경우 반환 status는
 * `CONFIT_ERR_COMPATIBILITY`이고, `out_report`는 caller가 소유한다.
 *
 * @param suite 로드된 compatibility assertions.
 * @param projects resolved project 목록.
 * @param project_count resolved project 개수.
 * @param out_report 성공 또는 compatibility failure 시 report를 받는다.
 * @param diagnostic 실패 시 첫 오류 위치와 메시지를 받는다.
 * @return compatible하면 CONFIT_OK, mismatch면 CONFIT_ERR_COMPATIBILITY.
 */
ConfitStatus confit_compat_check_report(const ConfitCompatSuite *suite,
                                        const ConfitCompatProject *projects,
                                        size_t project_count,
                                        ConfitCompatReport **out_report,
                                        ConfitDiagnostic *diagnostic);

/**
 * @brief compatibility suite와 resolved project들을 검사한다.
 *
 * @param suite 로드된 compatibility assertions.
 * @param projects resolved project 목록.
 * @param project_count resolved project 개수.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받는다.
 * @return compatible하면 CONFIT_OK, mismatch면 CONFIT_ERR_COMPATIBILITY.
 */
ConfitStatus confit_compat_check(const ConfitCompatSuite *suite,
                                 const ConfitCompatProject *projects,
                                 size_t project_count,
                                 ConfitDiagnostic *diagnostic);

/**
 * @brief compatibility report를 deterministic JSON 문자열로 직렬화한다.
 *
 * 반환된 문자열은 caller가 소유하며 `confit_compat_string_free`로 해제한다.
 *
 * @param report 직렬화할 report.
 * @param out_json 성공 시 NUL 종료 JSON 문자열을 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_compat_report_to_json(const ConfitCompatReport *report,
                                          char **out_json);

/**
 * @brief compatibility report ownership tree를 해제한다.
 *
 * @param report 해제할 report. `NULL`은 허용한다.
 */
void confit_compat_report_free(ConfitCompatReport *report);

/**
 * @brief compatibility suite와 하위 ownership tree를 해제한다.
 *
 * @param suite 해제할 suite. `NULL`은 허용한다.
 */
void confit_compat_suite_free(ConfitCompatSuite *suite);

/**
 * @brief compat module이 caller에게 넘긴 문자열 allocation을 해제한다.
 *
 * @param text 해제할 문자열. `NULL`은 허용한다.
 */
void confit_compat_string_free(char *text);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_COMPAT_H */
