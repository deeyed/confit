#ifndef CONFIT_SNAPSHOT_H
#define CONFIT_SNAPSHOT_H

#include "confit/project.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief version-specific resolved configuration을 감추는 immutable handle이다. */
typedef struct ConfitSnapshotHandle ConfitSnapshotHandle;

/**
 * @brief opaque resolved snapshot과 version-specific ownership tree를 해제한다.
 *
 * @param snapshot 해제할 handle. NULL은 허용한다.
 */
void confit_snapshot_handle_free(ConfitSnapshotHandle *snapshot);

/**
 * @brief snapshot handle이 보유한 schema version을 반환한다.
 *
 * @param snapshot 조회할 handle. NULL이면 CONFIT_SCHEMA_VERSION_INVALID.
 * @return immutable snapshot의 schema version.
 */
ConfitSchemaVersion
confit_snapshot_handle_schema_version(const ConfitSnapshotHandle *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_SNAPSHOT_H */
