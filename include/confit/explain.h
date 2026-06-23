#ifndef CONFIT_EXPLAIN_H
#define CONFIT_EXPLAIN_H

#include "confit/diagnostic.h"
#include "confit/model.h"
#include "confit/resolver.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief resolved option 하나에 대한 사람이 읽는 explanation을 생성한다.
 *
 * 출력은 deterministic text이며 option의 enabled/disabled 상태, resolved
 * value, provenance, outgoing dependency, incoming dependency trace를 담는다.
 * 반환된 문자열은 caller가 소유하며 `confit_explain_string_free`로 해제한다.
 *
 * @param project source project model.
 * @param config resolver가 생성한 resolved config.
 * @param option_id 설명할 option id.
 * @param out_text 성공 시 NUL 종료 explanation text를 받는다.
 * @param diagnostic 실패 시 오류 위치와 메시지를 받는다.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_explain_option(const ConfitProject *project,
                                   const ConfitResolvedConfig *config,
                                   const char *option_id, char **out_text,
                                   ConfitDiagnostic *diagnostic);

/**
 * @brief explain module이 caller에게 넘긴 문자열 allocation을 해제한다.
 *
 * @param text 해제할 문자열. `NULL`은 허용한다.
 */
void confit_explain_string_free(char *text);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_EXPLAIN_H */
