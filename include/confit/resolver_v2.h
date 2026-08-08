#ifndef CONFIT_RESOLVER_V2_H
#define CONFIT_RESOLVER_V2_H

#include <stddef.h>
#include <stdint.h>

#include "confit/constraint_v2.h"

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

/** @brief TUI 같은 profile writer가 transaction 안에서 제공하는 raw override다. */
typedef struct ConfitV2ProfileOverride {
  /** canonical option id다. */
  const char *option_id;
  /** type별 TOML-compatible literal이다. string/path/enum은 unquoted text도 허용한다. */
  const char *value_text;
  /** caller가 제공하는 transaction source 위치다. */
  ConfitV2SourceSpan span;
} ConfitV2ProfileOverride;

/** @brief v2 ledger build 요청이다. 모든 pointer는 호출 동안 유효해야 한다. */
typedef struct ConfitV2LedgerOptions {
  /** selected leaf profile name. 없으면 profile lane을 읽지 않는다. */
  const char *profile_name;
  /** explicit selected target. 없으면 profile/default target을 사용한다. */
  const char *target_name;
  /** explicit target 선택 source다. 비어 있으면 `cli --target`으로 기록한다. */
  ConfitV2SourceSpan target_span;
  /** profile document보다 높은 우선순위로 preview하는 mutable profile transaction이다. */
  const ConfitV2ProfileOverride *profile_overrides;
  size_t profile_override_count;
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

/** @brief effective value가 선택된 semantic source다. */
typedef enum ConfitV2EffectiveValueOrigin {
  CONFIT_V2_EFFECTIVE_VALUE_REQUESTED = 1,
  CONFIT_V2_EFFECTIVE_VALUE_CONDITIONAL_DEFAULT,
  CONFIT_V2_EFFECTIVE_VALUE_DEFAULT,
  CONFIT_V2_EFFECTIVE_VALUE_COMPUTED,
  CONFIT_V2_EFFECTIVE_VALUE_UNSET,
} ConfitV2EffectiveValueOrigin;

/** @brief one option의 immutable effective candidate와 provenance다. */
typedef struct ConfitV2EffectiveValue {
  const ConfitV2Symbol *symbol;
  ConfitV2Value value;
  int is_set;
  /** semantic eligibility. false여도 disabled/unset candidate는 보존한다. */
  int available;
  /** presentation eligibility이며 effective value를 변경하지 않는다. */
  int visible;
  ConfitV2EffectiveValueOrigin origin;
  const ConfitV2LedgerEntry *requested;
  const char *source_path;
  size_t source_line;
  size_t source_column;
} ConfitV2EffectiveValue;

/** @brief choice selection이 생긴 semantic source다. */
typedef enum ConfitV2ChoiceSelectionOrigin {
  CONFIT_V2_CHOICE_SELECTION_NONE = 0,
  CONFIT_V2_CHOICE_SELECTION_EFFECTIVE_MEMBER,
  CONFIT_V2_CHOICE_SELECTION_DEFAULT,
} ConfitV2ChoiceSelectionOrigin;

/** @brief one choice의 immutable validation/selection result다. */
typedef struct ConfitV2ChoiceResolution {
  const ConfitV2CompiledChoice *choice;
  /** choice 자체의 semantic eligibility다. */
  int available;
  /** TUI/document presentation eligibility다. */
  int visible;
  /** 현재 effective state에서 selected인 available member 수다. */
  size_t effective_member_count;
  /** effective member 또는 conditional default가 고른 member다. */
  const ConfitV2Symbol *selected_member;
  ConfitV2ChoiceSelectionOrigin origin;
} ConfitV2ChoiceResolution;

/** @brief requested ledger를 계산한 v2 effective-value handle이다. */
typedef struct ConfitV2Evaluation ConfitV2Evaluation;

/**
 * @brief v2 profile/target/profile transaction/user request를 typed deterministic ledger로 수집한다.
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
 * @brief requested ledger를 conditional default/computed DAG 순서로 계산한다.
 *
 * Availability/visibility와 choice selection/cardinality를 함께 계산한다. Named
 * constraint와 snapshot freeze는 이 API의 범위 밖이다. 실패하면 partial
 * evaluation을 반환하지 않는다. `ledger`는 반환된 evaluation보다 오래 유지해야
 * 한다.
 */
ConfitStatus confit_v2_evaluation_build(const ConfitV2AssignmentLedger *ledger,
                                         ConfitV2Evaluation **out_evaluation,
                                         ConfitDiagnostic *diagnostic);

/** @brief effective value handle과 owned payload를 해제한다. */
void confit_v2_evaluation_free(ConfitV2Evaluation *evaluation);

/** @brief evaluation이 borrow하는 requested ledger를 반환한다. */
const ConfitV2AssignmentLedger *confit_v2_evaluation_source(
    const ConfitV2Evaluation *evaluation);

/** @brief canonical option id lexical order의 effective value 개수다. */
size_t confit_v2_evaluation_value_count(const ConfitV2Evaluation *evaluation);

/** @brief canonical effective-value index를 반환한다. */
const ConfitV2EffectiveValue *confit_v2_evaluation_value_at(
    const ConfitV2Evaluation *evaluation, size_t index);

/** @brief canonical option id의 effective value를 반환한다. */
const ConfitV2EffectiveValue *confit_v2_evaluation_find(
    const ConfitV2Evaluation *evaluation, const char *option_id);

/** @brief canonical choice id lexical order의 choice result 개수다. */
size_t confit_v2_evaluation_choice_count(const ConfitV2Evaluation *evaluation);

/** @brief canonical choice result를 반환한다. */
const ConfitV2ChoiceResolution *confit_v2_evaluation_choice_at(
    const ConfitV2Evaluation *evaluation, size_t index);

/** @brief canonical choice id의 result를 반환한다. */
const ConfitV2ChoiceResolution *confit_v2_evaluation_find_choice(
    const ConfitV2Evaluation *evaluation, const char *choice_id);

/**
 * @brief final effective context에서 named constraint를 검증한다.
 *
 * Constraint가 하나라도 실패하면 report는 반환되지만 status는
 * `CONFIT_ERR_SCHEMA`이며 caller는 성공 snapshot을 publish하면 안 된다.
 */
ConfitStatus confit_v2_evaluation_validate_constraints(
    const ConfitV2Evaluation *evaluation,
    ConfitV2ConstraintReport **out_report, ConfitDiagnostic *diagnostic);

/** @brief deterministic effective-value hash를 계산한다. */
ConfitStatus confit_v2_evaluation_hash(const ConfitV2Evaluation *evaluation,
                                        uint64_t *out_hash);

/** @brief v2 resolve의 publish 가능한 immutable result다. */
typedef struct ConfitV2Snapshot ConfitV2Snapshot;

/**
 * @brief resolved profile/target inheritance에서 component root set을 bounded하게 수집한다.
 *
 * Returned strings are caller-owned and `confit_host_string_list_free()`로 해제한다.
 */
ConfitStatus confit_v2_snapshot_collect_component_roots(
    const ConfitV2CompiledStructure *compiled, const ConfitV2Snapshot *snapshot,
    char ***out_roots, size_t *out_count, ConfitDiagnostic *diagnostic);

/**
 * @brief resolved profile/target inheritance의 required/optional capability request를 bounded하게 수집한다.
 *
 * Returned lists are caller-owned and 각각 `confit_host_string_list_free()`로 해제한다.
 * Required request는 provider absence를 실패로 만들고 optional request는 absence를 정상으로 남긴다.
 */
ConfitStatus confit_v2_snapshot_collect_component_capability_requests(
    const ConfitV2CompiledStructure *compiled, const ConfitV2Snapshot *snapshot,
    char ***out_required, size_t *out_required_count, char ***out_optional,
    size_t *out_optional_count, ConfitDiagnostic *diagnostic);

/** @brief requested/effective assignment가 온 semantic lane이다. */
typedef enum ConfitV2ProvenanceKind {
  CONFIT_V2_PROVENANCE_SCHEMA_DEFAULT = 1,
  CONFIT_V2_PROVENANCE_CONDITIONAL_DEFAULT,
  CONFIT_V2_PROVENANCE_PROFILE_ASSIGNMENT,
  CONFIT_V2_PROVENANCE_TARGET_ASSIGNMENT,
  CONFIT_V2_PROVENANCE_USER_ASSIGNMENT,
  CONFIT_V2_PROVENANCE_COMPUTED,
  CONFIT_V2_PROVENANCE_CHOICE_DECISION,
  CONFIT_V2_PROVENANCE_CONSTRAINT,
  CONFIT_V2_PROVENANCE_UNSET,
  CONFIT_V2_PROVENANCE_EFFECTIVE_VALUE,
} ConfitV2ProvenanceKind;

/** @brief snapshot이 소유하는 one requested/effective value provenance다. */
typedef struct ConfitV2SnapshotAssignment {
  int is_present;
  int is_set;
  int is_unset;
  ConfitV2AssignmentOrigin origin;
  ConfitV2Value value;
  const char *source_path;
  size_t source_line;
  size_t source_column;
} ConfitV2SnapshotAssignment;

/** @brief canonical option id 순서의 fully-owned v2 snapshot option이다. */
typedef struct ConfitV2SnapshotOption {
  const char *id;
  ConfitV2OptionType type;
  ConfitV2WriteDomain write_domain;
  unsigned int emit_mask;
  int available;
  int visible;
  ConfitV2SnapshotAssignment requested;
  int effective_is_set;
  ConfitV2EffectiveValueOrigin effective_origin;
  ConfitV2Value effective_value;
  const char *effective_source_path;
  size_t effective_source_line;
  size_t effective_source_column;
} ConfitV2SnapshotOption;

/** @brief canonical choice id 순서의 fully-owned choice state다. */
typedef struct ConfitV2SnapshotChoice {
  const char *id;
  int available;
  int visible;
  size_t effective_member_count;
  const char *selected_member_id;
  ConfitV2ChoiceSelectionOrigin origin;
} ConfitV2SnapshotChoice;

/** @brief named constraint의 frozen outcome이다. */
typedef struct ConfitV2SnapshotConstraint {
  const char *id;
  ConfitV2ConstraintOutcome outcome;
  const char *message;
  const char *source_path;
  size_t source_line;
  size_t source_column;
} ConfitV2SnapshotConstraint;

/** @brief explanation graph의 node다. snapshot이 모든 text를 소유한다. */
typedef struct ConfitV2ProvenanceNode {
  ConfitV2ProvenanceKind kind;
  const char *subject_id;
  const char *source_path;
  size_t source_line;
  size_t source_column;
} ConfitV2ProvenanceNode;

/** @brief provenance node index를 잇는 causal edge다. */
typedef struct ConfitV2ProvenanceEdge {
  size_t from_index;
  size_t to_index;
} ConfitV2ProvenanceEdge;

/** @brief edit가 invalidate할 semantic node 종류다. */
typedef enum ConfitV2InvalidationKind {
  CONFIT_V2_INVALIDATION_OPTION = 1,
  CONFIT_V2_INVALIDATION_CHOICE,
  CONFIT_V2_INVALIDATION_CONSTRAINT,
} ConfitV2InvalidationKind;

/** @brief deterministic invalidation set의 one node다. */
typedef struct ConfitV2InvalidationNode {
  ConfitV2InvalidationKind kind;
  const char *id;
} ConfitV2InvalidationNode;

/** @brief snapshot이 만든 caller-owned invalidation result다. */
typedef struct ConfitV2InvalidationSet ConfitV2InvalidationSet;

/**
 * @brief validated ledger/evaluation/report를 source lifetime과 분리된 snapshot으로
 *        freeze한다.
 *
 * `report`는 성공한 complete constraint report여야 한다. constraint failure가
 * 있으면 snapshot publish는 거부된다. 반환 snapshot은 ledger, evaluation,
 * report, compiled/project가 해제된 뒤에도 그 공개 data를 계속 보유한다.
 */
ConfitStatus confit_v2_snapshot_freeze(
    const ConfitV2AssignmentLedger *ledger,
    const ConfitV2Evaluation *evaluation,
    const ConfitV2ConstraintReport *report, ConfitV2Snapshot **out_snapshot,
    ConfitDiagnostic *diagnostic);

/** @brief link/compile/ledger/evaluate/constraint를 거쳐 publish snapshot을 만든다. */
ConfitStatus confit_v2_snapshot_resolve(
    const ConfitV2CompiledStructure *compiled,
    const ConfitV2LedgerOptions *options, ConfitV2Snapshot **out_snapshot,
    ConfitDiagnostic *diagnostic);

/** @brief snapshot과 내부 provenance/reverse index를 해제한다. */
void confit_v2_snapshot_free(ConfitV2Snapshot *snapshot);

/** @brief source schema semantic hash, selected input hash, final semantic hash다. */
uint64_t confit_v2_snapshot_source_hash(const ConfitV2Snapshot *snapshot);
uint64_t confit_v2_snapshot_input_hash(const ConfitV2Snapshot *snapshot);
uint64_t confit_v2_snapshot_semantic_hash(const ConfitV2Snapshot *snapshot);

/** @brief snapshot이 기록한 immutable project/selection identity다. */
const char *confit_v2_snapshot_project_name(const ConfitV2Snapshot *snapshot);
const char *confit_v2_snapshot_project_namespace(
    const ConfitV2Snapshot *snapshot);
const char *confit_v2_snapshot_project_version(
    const ConfitV2Snapshot *snapshot);
/** @brief source provenance를 project-relative label로 바꿀 때 쓸 config root다. */
const char *confit_v2_snapshot_source_root(const ConfitV2Snapshot *snapshot);
const char *confit_v2_snapshot_profile_name(const ConfitV2Snapshot *snapshot);
const char *confit_v2_snapshot_target_name(const ConfitV2Snapshot *snapshot);

/** @brief canonical option/choice/constraint record를 조회한다. */
size_t confit_v2_snapshot_option_count(const ConfitV2Snapshot *snapshot);
const ConfitV2SnapshotOption *confit_v2_snapshot_option_at(
    const ConfitV2Snapshot *snapshot, size_t index);
const ConfitV2SnapshotOption *confit_v2_snapshot_find_option(
    const ConfitV2Snapshot *snapshot, const char *option_id);
size_t confit_v2_snapshot_choice_count(const ConfitV2Snapshot *snapshot);
const ConfitV2SnapshotChoice *confit_v2_snapshot_choice_at(
    const ConfitV2Snapshot *snapshot, size_t index);
size_t confit_v2_snapshot_constraint_count(const ConfitV2Snapshot *snapshot);
const ConfitV2SnapshotConstraint *confit_v2_snapshot_constraint_at(
    const ConfitV2Snapshot *snapshot, size_t index);

/** @brief frozen causal explanation graph를 canonical insertion order로 조회한다. */
size_t confit_v2_snapshot_provenance_node_count(
    const ConfitV2Snapshot *snapshot);
const ConfitV2ProvenanceNode *confit_v2_snapshot_provenance_node_at(
    const ConfitV2Snapshot *snapshot, size_t index);
size_t confit_v2_snapshot_provenance_edge_count(
    const ConfitV2Snapshot *snapshot);
const ConfitV2ProvenanceEdge *confit_v2_snapshot_provenance_edge_at(
    const ConfitV2Snapshot *snapshot, size_t index);

/** @brief changed option에서 시작하는 evaluation/visibility/choice/constraint closure다. */
ConfitStatus confit_v2_snapshot_invalidate(
    const ConfitV2Snapshot *snapshot, const char *changed_option_id,
    ConfitV2InvalidationSet **out_set, ConfitDiagnostic *diagnostic);
void confit_v2_invalidation_set_free(ConfitV2InvalidationSet *set);
size_t confit_v2_invalidation_set_count(const ConfitV2InvalidationSet *set);
const ConfitV2InvalidationNode *confit_v2_invalidation_set_at(
    const ConfitV2InvalidationSet *set, size_t index);

/**
 * @brief edit invalidation closure를 계산한 뒤 full resolver와 byte-identical
 *        snapshot을 만든 correctness-first incremental transaction이다.
 *
 * 현재 구현은 partial evaluator를 publish하지 않는다. affected set은 이후
 * partial evaluation이 갱신할 정확한 closure이며, 반환 snapshot은 항상 full
 * resolve와 동일한 immutable result다.
 */
ConfitStatus confit_v2_snapshot_reconcile_edit(
    const ConfitV2Snapshot *base,
    const ConfitV2CompiledStructure *compiled,
    const ConfitV2LedgerOptions *options, const char *changed_option_id,
    ConfitV2Snapshot **out_snapshot, ConfitV2InvalidationSet **out_affected,
    ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_RESOLVER_V2_H */
