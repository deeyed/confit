#ifndef CONFIT_RESOLVER_V2_H
#define CONFIT_RESOLVER_V2_H

#include "confit/snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief v2 resolver skeleton을 호출한다.
 *
 * v2 expression/model/resolution implementation 전에는 항상
 * CONFIT_ERR_UNSUPPORTED를 반환한다. 이 API는 v1 resolver에 v2 handle을 넘기는
 * 우회 경로를 제공하지 않는다.
 *
 * @param project v2 project handle.
 * @param out_snapshot 성공 시 caller-owned v2 snapshot handle. 현재는 항상 NULL.
 * @param diagnostic 실패 위치와 원인을 기록할 optional record.
 * @return 현재는 항상 CONFIT_ERR_UNSUPPORTED.
 */
ConfitStatus confit_resolver_v2_resolve_handle(
    const ConfitProjectHandle *project, ConfitSnapshotHandle **out_snapshot,
    ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_RESOLVER_V2_H */
