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
  const char *receipt_sha256;
  const ConfitV4ProductBinding *bindings;
  size_t binding_count;
  /** R03에서는 반드시 0이며 R05 producer만 1을 게시할 수 있다. */
  int trusted_external_producer;
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
 * @brief product-binding을 검사한 뒤 selected alias Apply를 시도한다.
 *
 * R03 candidate에는 trusted Bake producer가 없으므로 structurally valid synthetic
 * receipt도 `CONFIT_ERR_UNSUPPORTED`로 효과 없이 실패한다.
 */
ConfitStatus confit_v4_generation_apply(
    ConfitV4GenerationTransaction *transaction,
    const ConfitV4ProductBindingReceipt *receipt,
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
