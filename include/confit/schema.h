#ifndef CONFIT_SCHEMA_H
#define CONFIT_SCHEMA_H

#include "confit/diagnostic.h"
#include "confit/model.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Confit schema source tree를 읽어 project model을 만든다.
 *
 * `project_root`는 `config/project.toml`을 포함하는 project root이거나
 * `project.toml`을 직접 포함하는 config root일 수 있다. Loader는
 * `project.toml`의 imports 순서대로 option file을 읽은 뒤 profiles directory와
 * targets directory의 TOML file을 deterministic order로 읽어 model에 추가한다.
 *
 * @param project_root project root 또는 config root.
 * @param out_project 성공 시 caller-owned project를 받는다.
 * @param diagnostic 실패 시 schema 또는 parse 오류 위치를 기록한다.
 * @return 성공하면 CONFIT_OK, 실패하면 오류 status.
 */
ConfitStatus confit_schema_load_project(const char *project_root,
                                        ConfitProject **out_project,
                                        ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_SCHEMA_H */
