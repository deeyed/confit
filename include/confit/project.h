#ifndef CONFIT_PROJECT_H
#define CONFIT_PROJECT_H

#include "confit/diagnostic.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 공개 schema 구현 세대를 식별한다.
 *
 * Version-specific model은 이 enum과 opaque handle 뒤에 남는다. 호출자는 다른
 * schema version의 raw struct를 같은 코드 경로에서 섞어 사용할 수 없다.
 */
typedef enum ConfitSchemaVersion {
  /** bootstrap 단계에서 schema version을 읽지 못했거나 알 수 없다. */
  CONFIT_SCHEMA_VERSION_INVALID = 0,
  /** 안정화된 기존 schema version 1. */
  CONFIT_SCHEMA_VERSION_V1 = 1,
  /** hard-cut semantic model을 사용할 schema version 2. */
  CONFIT_SCHEMA_VERSION_V2 = 2,
} ConfitSchemaVersion;

/** @brief version-specific project model을 감추는 immutable public handle이다. */
typedef struct ConfitProjectHandle ConfitProjectHandle;

/**
 * @brief project bootstrap의 `[project].schema_version`을 읽어 loader를 분기한다.
 *
 * `project_root`는 `config/project.toml`을 포함하는 project root 또는
 * `project.toml`을 직접 포함하는 config root다. schema version은 source file만
 * authority로 삼으며 CLI/API argument로 override할 수 없다. 지원되는 v1/v2
 * loader는 각각의 opaque model handle을 반환하며, 지원하지 않는 version만
 * `CONFIT_ERR_UNSUPPORTED`와 명확한 diagnostic을 반환한다.
 *
 * @param project_root project root 또는 config root.
 * @param out_project 성공 시 caller-owned opaque project handle.
 * @param diagnostic 실패 위치와 원인을 기록할 optional record.
 * @return 성공하면 CONFIT_OK, 지원하지 않는 version이면 CONFIT_ERR_UNSUPPORTED.
 */
ConfitStatus confit_project_load(const char *project_root,
                                 ConfitProjectHandle **out_project,
                                 ConfitDiagnostic *diagnostic);

/**
 * @brief opaque project handle과 version-specific ownership tree를 해제한다.
 *
 * @param project 해제할 handle. NULL은 허용한다.
 */
void confit_project_handle_free(ConfitProjectHandle *project);

/**
 * @brief project handle이 보유한 schema version을 반환한다.
 *
 * @param project 조회할 handle. NULL이면 CONFIT_SCHEMA_VERSION_INVALID.
 * @return immutable project의 schema version.
 */
ConfitSchemaVersion
confit_project_handle_schema_version(const ConfitProjectHandle *project);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_PROJECT_H */
