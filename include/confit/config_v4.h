#ifndef CONFIT_CONFIG_V4_H
#define CONFIT_CONFIG_V4_H

#include <stddef.h>
#include <stdint.h>

#include "confit/diagnostic.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Config v4가 한 option에서 허용하는 prerequisite 상한이다. */
#define CONFIT_V4_MAX_OPTION_EDGES 128U

/** @brief Config v4 option의 closed value kind다. */
typedef enum ConfitV4OptionType {
  CONFIT_V4_OPTION_INVALID = 0,
  CONFIT_V4_OPTION_BOOL,
  CONFIT_V4_OPTION_PLACEMENT,
  CONFIT_V4_OPTION_ENUM,
  CONFIT_V4_OPTION_INTEGER,
  CONFIT_V4_OPTION_STRING,
} ConfitV4OptionType;

/** @brief selectable KPF provider가 게시하는 cardinality다. */
typedef enum ConfitV4ProviderCardinality {
  CONFIT_V4_PROVIDER_CARDINALITY_INVALID = 0,
  CONFIT_V4_PROVIDER_CARDINALITY_SINGLE,
  CONFIT_V4_PROVIDER_CARDINALITY_MULTIPLE,
} ConfitV4ProviderCardinality;

/** @brief selectable KPF provider 집합의 부재 정책이다. */
typedef enum ConfitV4ProviderAbsence {
  CONFIT_V4_PROVIDER_ABSENCE_INVALID = 0,
  CONFIT_V4_PROVIDER_ABSENCE_ALLOWED,
  CONFIT_V4_PROVIDER_ABSENCE_FORBIDDEN,
} ConfitV4ProviderAbsence;

/** @brief TUI choice가 허용하는 closed cardinality다. */
typedef enum ConfitV4ChoiceCardinality {
  CONFIT_V4_CHOICE_CARDINALITY_INVALID = 0,
  CONFIT_V4_CHOICE_AT_MOST_ONE,
  CONFIT_V4_CHOICE_EXACTLY_ONE,
} ConfitV4ChoiceCardinality;

/** @brief configure reason graph의 causal record 종류다. */
typedef enum ConfitV4ReasonKind {
  CONFIT_V4_REASON_INVALID = 0,
  CONFIT_V4_REASON_DEFAULT,
  CONFIT_V4_REASON_REQUEST,
  CONFIT_V4_REASON_PREREQUISITE,
  CONFIT_V4_REASON_VISIBILITY,
  CONFIT_V4_REASON_CHOICE,
  CONFIT_V4_REASON_RULE,
  CONFIT_V4_REASON_PROVIDER,
  CONFIT_V4_REASON_CYCLE,
  CONFIT_V4_REASON_AMBIGUITY,
} ConfitV4ReasonKind;

/** @brief catalog가 소유하는 immutable source span view다. */
typedef struct ConfitV4SourceSpan {
  const char *path;
  size_t line;
  size_t column;
} ConfitV4SourceSpan;

/** @brief option provenance와 TUI 표시 정보를 노출하는 borrowed view다. */
typedef struct ConfitV4OptionView {
  const char *symbol;
  const char *projection;
  ConfitV4OptionType type;
  const char *prompt;
  const char *help;
  const char *menu;
  int64_t menu_order;
  const char *owner;
  const char *since;
  const char *stability;
  const char *default_value;
  int64_t minimum;
  int64_t maximum;
  size_t domain_count;
  const char *const *domain_values;
  size_t enabled_value_count;
  const char *const *enabled_values;
  size_t tag_count;
  const char *const *tags;
  size_t prerequisite_count;
  const char *const *prerequisites;
  size_t visible_count;
  const char *const *visible_all;
  size_t provider_count;
  ConfitV4SourceSpan declaration;
  ConfitV4SourceSpan owner_source;
  ConfitV4SourceSpan since_source;
  ConfitV4SourceSpan stability_source;
  ConfitV4SourceSpan tags_source;
  ConfitV4SourceSpan menu_order_source;
  ConfitV4SourceSpan default_source;
} ConfitV4OptionView;

/** @brief evaluation에 전달하는 explicit option assignment다. */
typedef struct ConfitV4Assignment {
  const char *symbol;
  const char *value;
  ConfitV4SourceSpan source;
} ConfitV4Assignment;

/** @brief explicit base-to-leaf edge를 가진 configuration assignment다. */
typedef struct ConfitV4LayeredAssignment {
  ConfitV4Assignment assignment;
  /** 바로 이전 effective assignment의 exact source path다. */
  const char *overrides_source_path;
} ConfitV4LayeredAssignment;

/**
 * @brief `single` KPF provider의 explicit selection owner다.
 *
 * `option_symbol`은 이미 non-off로 선택된 provider option이어야 한다. 이
 * record는 runtime device binding이나 capability grant가 아니다.
 */
typedef struct ConfitV4ProviderChoice {
  const char *namespace_name;
  uint32_t major;
  const char *option_symbol;
  ConfitV4SourceSpan source;
} ConfitV4ProviderChoice;

/** @brief TUI와 감사 도구가 소비하는 causal reason view다. */
typedef struct ConfitV4ReasonView {
  ConfitV4ReasonKind kind;
  int satisfied;
  const char *subject;
  const char *cause;
  ConfitV4SourceSpan source;
} ConfitV4ReasonView;

/** @brief Config v4 catalog의 opaque owner다. */
typedef struct ConfitV4Catalog ConfitV4Catalog;

/** @brief Config v4 evaluation과 reason graph의 opaque owner다. */
typedef struct ConfitV4Evaluation ConfitV4Evaluation;

/**
 * @brief repository의 `config/project.toml`에서 v4 catalog를 bounded load한다.
 *
 * Loader는 project가 등록한 role root만 lexical traversal하며 symlink, path
 * escape, case-fold collision, duplicate symbol, wrong-role table와 budget
 * 초과를 partial catalog 없이 거부한다. Retired manifest fallback이나 converter는
 * 호출하지 않는다.
 */
ConfitStatus confit_v4_catalog_load(const char *repository_root,
                                    ConfitV4Catalog **out_catalog,
                                    ConfitDiagnostic *diagnostic);

/** @brief catalog와 catalog가 소유한 모든 view storage를 해제한다. */
void confit_v4_catalog_free(ConfitV4Catalog *catalog);

/** @brief catalog가 발견한 Config/OWNERS document 수를 반환한다. */
size_t confit_v4_catalog_document_count(const ConfitV4Catalog *catalog);

/** @brief catalog option 수를 반환한다. */
size_t confit_v4_catalog_option_count(const ConfitV4Catalog *catalog);

/** @brief menu 수를 반환한다. */
size_t confit_v4_catalog_menu_count(const ConfitV4Catalog *catalog);

/** @brief choice 수를 반환한다. */
size_t confit_v4_catalog_choice_count(const ConfitV4Catalog *catalog);

/** @brief global rule 수를 반환한다. */
size_t confit_v4_catalog_rule_count(const ConfitV4Catalog *catalog);

/**
 * @brief exact symbol option을 borrowed view로 조회한다.
 *
 * @return 찾으면 1, 없으면 0.
 */
int confit_v4_catalog_option(const ConfitV4Catalog *catalog,
                             const char *symbol,
                             ConfitV4OptionView *out_option);

/**
 * @brief explicit assignment와 provider choice를 검증해 reason graph를 만든다.
 *
 * `constraints.all`은 자동 enable을 하지 않는다. 실패하더라도 만들어진
 * causal reason은 `out_evaluation`에 보존되며 caller가 해제해야 한다.
 */
ConfitStatus confit_v4_evaluate(
    const ConfitV4Catalog *catalog, const ConfitV4Assignment *assignments,
    size_t assignment_count, const ConfitV4ProviderChoice *provider_choices,
    size_t provider_choice_count, ConfitV4Evaluation **out_evaluation,
    ConfitDiagnostic *diagnostic);

/**
 * @brief explicit override chain을 포함한 assignment를 평가한다.
 *
 * 같은 symbol이 다시 등장하면 현재 effective source path와 정확히 일치하는
 * `overrides_source_path`가 필요하다. Discovery order나 unrelated overwrite는 없다.
 */
ConfitStatus confit_v4_evaluate_layered(
    const ConfitV4Catalog *catalog,
    const ConfitV4LayeredAssignment *assignments, size_t assignment_count,
    const ConfitV4ProviderChoice *provider_choices,
    size_t provider_choice_count, ConfitV4Evaluation **out_evaluation,
    ConfitDiagnostic *diagnostic);

/** @brief evaluation과 reason storage를 해제한다. */
void confit_v4_evaluation_free(ConfitV4Evaluation *evaluation);

/** @brief option의 effective canonical value를 조회한다. */
const char *confit_v4_evaluation_value(const ConfitV4Evaluation *evaluation,
                                       const char *symbol);

/** @brief evaluation이 가진 effective option 수를 반환한다. */
size_t confit_v4_evaluation_value_count(
    const ConfitV4Evaluation *evaluation);

/**
 * @brief index의 effective symbol/value/source를 borrowed view로 반환한다.
 *
 * Generator는 이 ordered view만 소비하며 source/object/link graph를 조회하지 않는다.
 */
int confit_v4_evaluation_value_at(
    const ConfitV4Evaluation *evaluation, size_t index, const char **out_symbol,
    const char **out_value, int *out_enabled, ConfitV4SourceSpan *out_source);

/** @brief evaluation reason 수를 반환한다. */
size_t confit_v4_evaluation_reason_count(
    const ConfitV4Evaluation *evaluation);

/** @brief index의 reason을 borrowed view로 반환한다. */
int confit_v4_evaluation_reason(const ConfitV4Evaluation *evaluation,
                                size_t index,
                                ConfitV4ReasonView *out_reason);

/**
 * @brief resolved `single` provider option symbol을 조회한다.
 *
 * `multiple` provider group은 단일 result가 없으므로 이 API로 조회하지 않는다.
 */
const char *confit_v4_evaluation_single_provider(
    const ConfitV4Evaluation *evaluation, const char *namespace_name,
    uint32_t major);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_CONFIG_V4_H */
