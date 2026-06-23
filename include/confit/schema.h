#ifndef CONFIT_SCHEMA_H
#define CONFIT_SCHEMA_H

#include "confit/diagnostic.h"
#include "confit/model.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief schema audit warning 한 건이다.
 *
 * `path`, `message`, `option_id`는 audit 구조체가 소유한다.
 */
typedef struct ConfitSchemaWarning {
  /** warning source path. 없으면 `NULL`. */
  char *path;
  /** 1-based line number. 없으면 0. */
  size_t line;
  /** 1-based column number. 없으면 0. */
  size_t column;
  /** 관련 option id. 없으면 `NULL`. */
  char *option_id;
  /** 사람이 읽는 warning message. */
  char *message;
} ConfitSchemaWarning;

/**
 * @brief schema migration/stability audit 결과다.
 */
typedef struct ConfitSchemaAudit {
  /** warning 목록. */
  ConfitSchemaWarning *warnings;
  /** warning 개수. */
  size_t warning_count;
} ConfitSchemaAudit;

/**
 * @brief schema audit 구조체를 초기화한다.
 *
 * @param audit 초기화할 audit.
 */
void confit_schema_audit_init(ConfitSchemaAudit *audit);

/**
 * @brief schema audit warning 목록을 해제한다.
 *
 * @param audit 정리할 audit.
 */
void confit_schema_audit_clear(ConfitSchemaAudit *audit);

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

/**
 * @brief Confit schema source tree를 읽고 stability/migration audit도 수행한다.
 *
 * `audit`이 `NULL`이면 warning은 수집하지 않는다. 수집된 warning은 caller가
 * `confit_schema_audit_clear`로 해제한다.
 *
 * @param project_root project root 또는 config root.
 * @param out_project 성공 시 caller-owned project를 받는다.
 * @param audit optional audit output.
 * @param diagnostic 실패 시 schema 또는 parse 오류 위치를 기록한다.
 * @return 성공하면 CONFIT_OK, 실패하면 오류 status.
 */
ConfitStatus confit_schema_load_project_with_audit(
    const char *project_root, ConfitProject **out_project,
    ConfitSchemaAudit *audit, ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_SCHEMA_H */
