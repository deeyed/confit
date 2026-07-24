#ifndef CONFIT_SCHEMA_V2_H
#define CONFIT_SCHEMA_V2_H

#include "confit/project.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief v2 schema loader skeleton을 opaque project handle adapter로 호출한다.
 *
 * v2 loader/model 구현 전에는 항상 CONFIT_ERR_UNSUPPORTED를 반환한다. 이 명시적
 * 경계는 v1 raw model로 v2 project를 임시 해석하는 compatibility path를 금지한다.
 *
 * @param project_root bootstrap으로 확인한 v2 config root.
 * @param out_project 성공 시 caller-owned v2 project handle. 현재는 항상 NULL.
 * @param diagnostic 실패 위치와 원인을 기록할 optional record.
 * @return 현재는 항상 CONFIT_ERR_UNSUPPORTED.
 */
ConfitStatus confit_schema_v2_load_project_handle(
    const char *project_root, ConfitProjectHandle **out_project,
    ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_SCHEMA_V2_H */
