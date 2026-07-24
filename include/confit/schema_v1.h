#ifndef CONFIT_SCHEMA_V1_H
#define CONFIT_SCHEMA_V1_H

#include "confit/project.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief v1 schema loader를 opaque project handle adapter로 호출한다.
 *
 * 이 함수는 v1 raw model을 public API 밖에 유지한다. 전달된 source가 v1이 아닌
 * 경우에는 v1 loader의 schema diagnostic을 그대로 반환한다.
 *
 * @param project_root project root 또는 config root.
 * @param out_project 성공 시 caller-owned v1 project handle.
 * @param diagnostic 실패 위치와 원인을 기록할 optional record.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_schema_v1_load_project_handle(
    const char *project_root, ConfitProjectHandle **out_project,
    ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_SCHEMA_V1_H */
