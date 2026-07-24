#ifndef CONFIT_RESOLVER_V1_H
#define CONFIT_RESOLVER_V1_H

#include <stddef.h>

#include "confit/model.h"
#include "confit/snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief v1 project handle을 v1 resolver로 resolve해 opaque snapshot을 만든다.
 *
 * `project`가 v1 handle이 아니면 CONFIT_ERR_INVALID_ARGUMENT를 반환한다. User
 * override value type은 v1 model contract를 그대로 사용하며 v2 input model은 이
 * API에 추가하지 않는다.
 *
 * @param project v1 project handle.
 * @param profile_name optional selected profile name.
 * @param target_name optional selected target name.
 * @param user_values optional v1 user override 목록.
 * @param user_value_count user override 개수.
 * @param out_snapshot 성공 시 caller-owned v1 snapshot handle.
 * @param diagnostic 실패 위치와 원인을 기록할 optional record.
 * @return 성공하면 CONFIT_OK.
 */
ConfitStatus confit_resolver_v1_resolve_handle(
    const ConfitProjectHandle *project, const char *profile_name,
    const char *target_name, const ConfitNamedValue *user_values,
    size_t user_value_count, ConfitSnapshotHandle **out_snapshot,
    ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_RESOLVER_V1_H */
