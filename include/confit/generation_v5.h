#ifndef CONFIT_GENERATION_V5_H
#define CONFIT_GENERATION_V5_H

#include <stddef.h>

#include "confit/config_v5.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIT_V5_GENERATION_ARTIFACT_COUNT 7U
#define CONFIT_V5_GENERATION_DIGEST_TEXT 65U

/** @brief configure/verify 실행 파일의 measured identity다. */
typedef struct ConfitV5ToolIdentity {
  const char *path;
  const char *version;
  const char *sha256;
} ConfitV5ToolIdentity;

/** @brief immutable KERNCONF candidate를 만드는 데 필요한 전체 입력이다. */
typedef struct ConfitV5ConfigureRequest {
  const char *repository_root;
  const char *output_root;
  const char *architecture;
  const char *kernconf;
  const char *transaction_id;
  ConfitV5ToolIdentity resolver;
  ConfitV5ToolIdentity verifier;
} ConfitV5ConfigureRequest;

/** @brief transaction이 소유하는 generated artifact의 borrowed view다. */
typedef struct ConfitV5GeneratedArtifactView {
  const char *name;
  const char *text;
  size_t size;
  const char *sha256;
} ConfitV5GeneratedArtifactView;

typedef struct ConfitV5GenerationTransaction ConfitV5GenerationTransaction;

/**
 * @brief source inputs를 평가하고 아직 selected가 아닌 immutable candidate를 만든다.
 *
 * 성공한 transaction은 apply 또는 cancel로 정확히 한 번 닫아야 한다.
 */
ConfitStatus confit_v5_generation_preview(
    const ConfitV5ConfigureRequest *request,
    ConfitV5GenerationTransaction **out_transaction,
    ConfitDiagnostic *diagnostic);
/** @brief candidate를 selected pointer로 atomic publish한다. */
ConfitStatus confit_v5_generation_apply(
    ConfitV5GenerationTransaction *transaction,
    ConfitDiagnostic *diagnostic);
/** @brief 미적용 candidate를 제거하거나 적용된 transaction handle을 해제한다. */
ConfitStatus confit_v5_generation_cancel(
    ConfitV5GenerationTransaction **transaction,
    ConfitDiagnostic *diagnostic);
/** @brief transaction의 content-addressed generation digest를 빌려 반환한다. */
const char *confit_v5_generation_digest(
    const ConfitV5GenerationTransaction *transaction);
/** @brief index의 generated artifact를 transaction lifetime 동안 빌려준다. */
int confit_v5_generation_artifact(
    const ConfitV5GenerationTransaction *transaction, size_t index,
    ConfitV5GeneratedArtifactView *out_artifact);
/** @brief published candidate generation directory를 빌려 반환한다. */
const char *confit_v5_generation_directory(
    const ConfitV5GenerationTransaction *transaction);

/**
 * @brief resolver 없이 artifact, membership, build 축과 verifier identity를 재검증한다.
 *
 * 이 함수는 option semantics나 Make/source graph를 해석하지 않는다.
 * expected_architecture와 expected_kernconf는 현재 build invocation이 요구한
 * canonical atom이며 sealed selection과 byte-for-byte 같아야 한다.
 */
ConfitStatus confit_v5_configseal_verify(
    const char *generation_directory, const char *repository_root,
    const char *expected_architecture, const char *expected_kernconf,
    const ConfitV5ToolIdentity *verifier, ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_GENERATION_V5_H */
