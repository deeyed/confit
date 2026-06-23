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
 * @brief compatibility suite와 하위 ownership tree를 해제한다.
 *
 * @param suite 해제할 suite. `NULL`은 허용한다.
 */
void confit_compat_suite_free(ConfitCompatSuite *suite);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_COMPAT_H */
