#ifndef CONFIT_RESOLVER_V2_H
#define CONFIT_RESOLVER_V2_H

#include <stddef.h>
#include <stdint.h>

#include "confit/constraint_v2.h"
#include "confit/snapshot.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief v2 requested assignment가 생긴 source lane이다. */
typedef enum ConfitV2AssignmentOrigin {
  CONFIT_V2_ASSIGNMENT_ORIGIN_SCHEMA_DEFAULT = 1,
  CONFIT_V2_ASSIGNMENT_ORIGIN_PROFILE,
  CONFIT_V2_ASSIGNMENT_ORIGIN_TARGET,
  CONFIT_V2_ASSIGNMENT_ORIGIN_USER,
  CONFIT_V2_ASSIGNMENT_ORIGIN_UNSET,
} ConfitV2AssignmentOrigin;

/** @brief CLI/TUI가 전달하는 one raw user override다. */
typedef struct ConfitV2UserOverride {
  /** canonical option id다. */
  const char *option_id;
  /** type별 CLI literal이다. string/path/enum은 unquoted text도 허용한다. */
  const char *value_text;
  /** caller가 제공하는 input source 위치다. 없으면 source label을 사용한다. */
  ConfitV2SourceSpan span;
} ConfitV2UserOverride;

/** @brief v2 ledger build 요청이다. 모든 pointer는 호출 동안 유효해야 한다. */
typedef struct ConfitV2LedgerOptions {
  /** selected leaf profile name. 없으면 profile lane을 읽지 않는다. */
  const char *profile_name;
  /** explicit selected target. 없으면 profile/default target을 사용한다. */
  const char *target_name;
  /** explicit target 선택 source다. 비어 있으면 `cli --target`으로 기록한다. */
  ConfitV2SourceSpan target_span;
  /** typed parse 전의 user override text 목록이다. */
  const ConfitV2UserOverride *user_overrides;
  size_t user_override_count;
} ConfitV2LedgerOptions;

/** @brief target 이름 자체의 selection provenance다. */
typedef enum ConfitV2TargetSelectionOrigin {
  CONFIT_V2_TARGET_SELECTION_EXPLICIT = 1,
  CONFIT_V2_TARGET_SELECTION_PROFILE,
  CONFIT_V2_TARGET_SELECTION_PROJECT_DEFAULT,
} ConfitV2TargetSelectionOrigin;

/** @brief target-domain assignment와 별도로 보존하는 target 선택 record다. */
typedef struct ConfitV2TargetSelection {
  const char *name;
  ConfitV2TargetSelectionOrigin origin;
  const char *source_path;
  size_t source_line;
  size_t source_column;
} ConfitV2TargetSelection;

/** @brief provenance를 보존한 immutable requested assignment record다. */
typedef struct ConfitV2LedgerEntry {
  const ConfitV2Symbol *symbol;
  ConfitV2Value value;
  ConfitV2AssignmentOrigin origin;
  ConfitV2WriteDomain domain;
  int is_unset;
  int wins;
  size_t precedence;
  size_t declaration_order;
  const char *source_path;
  size_t source_line;
  size_t source_column;
} ConfitV2LedgerEntry;

/** @brief effective calculation 이전 requested assignment ledger다. */
typedef struct ConfitV2AssignmentLedger ConfitV2AssignmentLedger;

/**
 * @brief v2 profile/target/user request를 typed deterministic ledger로 수집한다.
 *
 * Conditional default, computed evaluation, availability, choice, constraint는
 * 수행하지 않는다. 모든 source assignment와 schema default를 보존하며 winning
 * request만 `wins`로 표시한다. `compiled`와 그 source project는 ledger보다 오래
 * 유지해야 한다.
 */
ConfitStatus confit_v2_assignment_ledger_build(
    const ConfitV2CompiledStructure *compiled,
    const ConfitV2LedgerOptions *options,
    ConfitV2AssignmentLedger **out_ledger, ConfitDiagnostic *diagnostic);

/** @brief ledger와 owned value/source records를 해제한다. */
void confit_v2_assignment_ledger_free(ConfitV2AssignmentLedger *ledger);

/** @brief source compiled structure를 반환한다. */
const ConfitV2CompiledStructure *confit_v2_assignment_ledger_source(
    const ConfitV2AssignmentLedger *ledger);

/** @brief canonical option id 및 precedence 순서의 assignment record 개수다. */
size_t confit_v2_assignment_ledger_entry_count(
    const ConfitV2AssignmentLedger *ledger);

/** @brief canonical ledger index의 assignment record를 반환한다. */
const ConfitV2LedgerEntry *confit_v2_assignment_ledger_entry_at(
    const ConfitV2AssignmentLedger *ledger, size_t index);

/** @brief option의 가장 높은 precedence requested record를 반환한다. */
const ConfitV2LedgerEntry *confit_v2_assignment_ledger_requested(
    const ConfitV2AssignmentLedger *ledger, const char *option_id);

/** @brief selected leaf profile name 또는 NULL을 반환한다. */
const char *confit_v2_assignment_ledger_profile_name(
    const ConfitV2AssignmentLedger *ledger);

/** @brief resolved target selection name 또는 NULL을 반환한다. */
const char *confit_v2_assignment_ledger_target_name(
    const ConfitV2AssignmentLedger *ledger);

/** @brief target name이 선택된 lane과 source를 반환한다. target이 없으면 NULL이다. */
const ConfitV2TargetSelection *confit_v2_assignment_ledger_target_selection(
    const ConfitV2AssignmentLedger *ledger);

/** @brief ordered ledger의 deterministic FNV-1a hash를 계산한다. */
ConfitStatus confit_v2_assignment_ledger_hash(
    const ConfitV2AssignmentLedger *ledger, uint64_t *out_hash);

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
