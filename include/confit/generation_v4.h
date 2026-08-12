#ifndef CONFIT_GENERATION_V4_H
#define CONFIT_GENERATION_V4_H

#include <stddef.h>

#include "confit/config_v4.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIT_V4_GENERATION_ARTIFACT_COUNT 8U
#define CONFIT_V4_GENERATION_DIGEST_TEXT 65U

/** @brief configure transaction이 봉인하는 executable identity다. */
typedef struct ConfitV4ToolIdentity {
  const char *path;
  const char *version;
  const char *sha256;
} ConfitV4ToolIdentity;

/** @brief configure-once Preview의 caller-owned immutable request다. */
typedef struct ConfitV4ConfigureRequest {
  const char *repository_root;
  const char *output_root;
  const char *profile_id;
  const char *target_id;
  const char *transaction_id;
  ConfitV4ToolIdentity resolver;
  ConfitV4ToolIdentity toolchain;
  ConfitV4ToolIdentity verifier;
  /** Bake product receipt를 발행할 유일한 compiled producer identity다. */
  ConfitV4ToolIdentity binding_producer;
  const ConfitV4LayeredAssignment *assignments;
  size_t assignment_count;
  const ConfitV4ProviderChoice *provider_choices;
  size_t provider_choice_count;
} ConfitV4ConfigureRequest;

/** @brief generated artifact 한 개의 canonical identity view다. */
typedef struct ConfitV4GeneratedArtifactView {
  const char *name;
  const char *text;
  size_t size;
  const char *sha256;
} ConfitV4GeneratedArtifactView;

/** @brief Preview부터 Cancel 또는 Apply까지의 opaque transaction owner다. */
typedef struct ConfitV4GenerationTransaction ConfitV4GenerationTransaction;

/** @brief Bake no-effect binding receipt의 one option/value/product row다. */
typedef struct ConfitV4ProductBinding {
  const char *symbol;
  const char *value;
  const char *product_role;
  const char *canonical_product;
} ConfitV4ProductBinding;

/** @brief external Bake product-binding receipt의 typed view다. */
typedef struct ConfitV4ProductBindingReceipt {
  const char *schema;
  const char *generation_sha256;
  const char *producer_path;
  const char *producer_version;
  const char *producer_sha256;
  const char *receipt_sha256;
  const ConfitV4ProductBinding *bindings;
  size_t binding_count;
} ConfitV4ProductBindingReceipt;

/**
 * @brief configuration을 평가하고 immutable candidate generation을 게시한다.
 *
 * Preview는 `selected` alias를 만들거나 바꾸지 않는다. Makefile/source/test graph는
 * 입력에도 output에도 존재하지 않는다.
 */
ConfitStatus confit_v4_generation_preview(
    const ConfitV4ConfigureRequest *request,
    ConfitV4GenerationTransaction **out_transaction,
    ConfitDiagnostic *diagnostic);

/** @brief Preview transaction의 candidate generation을 폐기하고 owner를 해제한다. */
ConfitStatus confit_v4_generation_cancel(
    ConfitV4GenerationTransaction **transaction,
    ConfitDiagnostic *diagnostic);

/**
 * @brief descriptor-rooted Bake receipt를 검사하고 selected alias를 게시한다.
 *
 * Caller가 구성한 typed struct나 boolean은 Apply 권한이 아니다. Preview에서 봉인한
 * producer가 현재 생성한 canonical receipt 파일만 성공할 수 있다.
 */
ConfitStatus confit_v4_generation_apply_file(
    ConfitV4GenerationTransaction *transaction,
    const char *receipt_path,
    ConfitDiagnostic *diagnostic);

/** @brief transaction의 generation digest를 borrowed string으로 반환한다. */
const char *confit_v4_generation_digest(
    const ConfitV4GenerationTransaction *transaction);

/** @brief index의 generated artifact view를 반환한다. */
int confit_v4_generation_artifact(
    const ConfitV4GenerationTransaction *transaction, size_t index,
    ConfitV4GeneratedArtifactView *out_artifact);

/** @brief candidate generation의 canonical directory path를 반환한다. */
const char *confit_v4_generation_directory(
    const ConfitV4GenerationTransaction *transaction);

/** @brief Preview가 요구하는 canonical product binding 수를 반환한다. */
size_t confit_v4_generation_binding_count(
    const ConfitV4GenerationTransaction *transaction);

/** @brief index의 expected product binding을 borrowed view로 반환한다. */
int confit_v4_generation_binding(
    const ConfitV4GenerationTransaction *transaction, size_t index,
    ConfitV4ProductBinding *out_binding);

/** @brief typed product-binding receipt의 structure와 digest를 검증한다. */
ConfitStatus confit_v4_product_binding_receipt_verify(
    const ConfitV4GenerationTransaction *transaction,
    const ConfitV4ProductBindingReceipt *receipt,
    ConfitDiagnostic *diagnostic);

/**
 * @brief immutable generation directory와 current inputs/tool identity를 검증한다.
 *
 * Resolver를 호출하거나 stale generation을 재생성하지 않는다.
 */
ConfitStatus confit_v4_configseal_verify(
    const char *generation_directory, const char *repository_root,
    const ConfitV4ToolIdentity *toolchain,
    const ConfitV4ToolIdentity *verifier, ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_GENERATION_V4_H */
