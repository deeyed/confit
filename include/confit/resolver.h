#ifndef CONFIT_RESOLVER_H
#define CONFIT_RESOLVER_H

#include <stddef.h>
#include <stdint.h>

#include "confit/diagnostic.h"
#include "confit/model.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief resolved option value와 provenance다.
 *
 * `option_id`, `value`, `source`는 resolved config가 소유한다.
 */
typedef struct ConfitResolvedValue {
  /** option id. */
  char *option_id;
  /** resolved value. */
  ConfitValue value;
  /** value source label. */
  char *source;
} ConfitResolvedValue;

/**
 * @brief resolver가 만든 deterministic resolved config다.
 *
 * `values`는 option id lexical order로 정렬된다.
 */
typedef struct ConfitResolvedConfig {
  /** resolved value 목록. */
  ConfitResolvedValue *values;
  /** resolved value 개수. */
  size_t value_count;
} ConfitResolvedConfig;

/**
 * @brief profile과 target/user override를 merge해 resolved config를 만든다.
 *
 * 적용 순서는 default, base profile chain, target, selected profile,
 * user override다. `target_name`이 `NULL`이면 selected profile의 target을
 * 사용한다. User override도 dependency/conflict 검증을 통과해야 한다.
 *
 * @param project source project model.
 * @param profile_name 선택 profile name. 없으면 default만 resolve한다.
 * @param target_name 명시 target name. 없으면 profile target을 사용한다.
 * @param user_values optional user override 목록.
 * @param user_value_count user override 개수.
 * @param out_config 성공 시 caller-owned resolved config를 받는다.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_resolver_resolve(
    const ConfitProject *project, const char *profile_name,
    const char *target_name, const ConfitNamedValue *user_values,
    size_t user_value_count, ConfitResolvedConfig **out_config,
    ConfitDiagnostic *diagnostic);

/**
 * @brief resolved config와 그 하위 ownership tree를 해제한다.
 *
 * @param config 해제할 resolved config. `NULL`은 허용한다.
 */
void confit_resolved_config_free(ConfitResolvedConfig *config);

/**
 * @brief option id로 resolved value를 찾는다.
 *
 * @param config 조회할 resolved config.
 * @param option_id 찾을 option id.
 * @return 찾으면 resolved value pointer, 없으면 `NULL`.
 */
const ConfitResolvedValue *confit_resolved_config_find(
    const ConfitResolvedConfig *config, const char *option_id);

/**
 * @brief resolved config를 deterministic JSON 문자열로 직렬화한다.
 *
 * 반환된 문자열은 caller가 소유하며 `confit_resolver_string_free`로 해제한다.
 *
 * @param config 직렬화할 resolved config.
 * @param out_json 성공 시 NUL 종료 JSON 문자열을 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_resolved_config_to_json(const ConfitResolvedConfig *config,
                                            char **out_json);

/**
 * @brief resolved config JSON의 deterministic FNV-1a hash를 계산한다.
 *
 * @param config hash할 resolved config.
 * @param out_hash 성공 시 64-bit hash를 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_resolved_config_hash(const ConfitResolvedConfig *config,
                                         uint64_t *out_hash);

/**
 * @brief resolver module이 caller에게 넘긴 문자열 allocation을 해제한다.
 *
 * @param text 해제할 문자열. `NULL`은 허용한다.
 */
void confit_resolver_string_free(char *text);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_RESOLVER_H */
