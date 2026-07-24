#ifndef CONFIT_MIGRATION_V2_H
#define CONFIT_MIGRATION_V2_H

#include "confit/diagnostic.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief source v1 tree를 건드리지 않는 V2 candidate migration 요청이다. */
typedef struct ConfitV1ToV2MigrationOptions {
  /** `config/project.toml`을 포함하는 V1 project root 또는 config root다. */
  const char *source_project_root;
  /** 새 candidate tree를 만들 별도 output root다. */
  const char *output_root;
} ConfitV1ToV2MigrationOptions;

/**
 * @brief V1 schema를 read-only로 분석해 separate V2 candidate를 생성한다.
 *
 * 자동 변환할 수 있는 option type/default/range/enum/metadata만 candidate에
 * 기록한다. dependency, profile, target, force, writer conflict처럼 V2의
 * explicit semantic 결정을 요구하는 source는 migration report의 TODO로 남긴다.
 * source root와 같은 output은 hard error이며 source file을 쓰지 않는다.
 */
ConfitStatus confit_v1_migrate_to_v2_candidate(
    const ConfitV1ToV2MigrationOptions *options, ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_MIGRATION_V2_H */
