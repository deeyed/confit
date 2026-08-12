#ifndef CONFIT_CONFIG_V5_H
#define CONFIT_CONFIG_V5_H

#include <stddef.h>
#include <stdint.h>

#include "confit/diagnostic.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CONFIT_V5_MAX_OPTION_EDGES 128U

/** @brief Config v5가 허용하는 사용자 선택 값의 폐쇄형 종류다. */
typedef enum ConfitV5OptionType {
  CONFIT_V5_OPTION_INVALID = 0,
  CONFIT_V5_OPTION_BOOL,
  CONFIT_V5_OPTION_PLACEMENT,
  CONFIT_V5_OPTION_ENUM,
  CONFIT_V5_OPTION_INTEGER,
  CONFIT_V5_OPTION_STRING,
} ConfitV5OptionType;

/** @brief 한 choice에서 동시에 활성화할 수 있는 member 수 계약이다. */
typedef enum ConfitV5ChoiceCardinality {
  CONFIT_V5_CHOICE_CARDINALITY_INVALID = 0,
  CONFIT_V5_CHOICE_AT_MOST_ONE,
  CONFIT_V5_CHOICE_EXACTLY_ONE,
} ConfitV5ChoiceCardinality;

/** @brief 평가 결과가 값을 채택하거나 거부한 원인을 분류한다. */
typedef enum ConfitV5ReasonKind {
  CONFIT_V5_REASON_INVALID = 0,
  CONFIT_V5_REASON_DEFAULT,
  CONFIT_V5_REASON_REQUEST,
  CONFIT_V5_REASON_PREREQUISITE,
  CONFIT_V5_REASON_VISIBILITY,
  CONFIT_V5_REASON_CHOICE,
  CONFIT_V5_REASON_RULE,
  CONFIT_V5_REASON_CYCLE,
  CONFIT_V5_REASON_AMBIGUITY,
} ConfitV5ReasonKind;

/** @brief 진단과 TUI provenance가 빌려 보는 immutable source 위치다. */
typedef struct ConfitV5SourceSpan {
  const char *path;
  size_t line;
  size_t column;
} ConfitV5SourceSpan;

/** @brief catalog가 소유하는 option metadata의 borrowed view다. */
typedef struct ConfitV5OptionView {
  const char *symbol;
  const char *projection;
  ConfitV5OptionType type;
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
  ConfitV5SourceSpan declaration;
  ConfitV5SourceSpan owner_source;
  ConfitV5SourceSpan since_source;
  ConfitV5SourceSpan stability_source;
  ConfitV5SourceSpan tags_source;
  ConfitV5SourceSpan menu_order_source;
  ConfitV5SourceSpan default_source;
} ConfitV5OptionView;

/** @brief catalog가 소유하는 shallow menu의 borrowed view다. */
typedef struct ConfitV5MenuView {
  const char *id;
  const char *prompt;
  const char *help;
  const char *parent;
  int64_t order;
  ConfitV5SourceSpan declaration;
} ConfitV5MenuView;

/** @brief KERNCONF 또는 preview 요청이 제공하는 단일 symbol writer다. */
typedef struct ConfitV5Assignment {
  const char *symbol;
  const char *value;
  ConfitV5SourceSpan source;
} ConfitV5Assignment;

/** @brief 평가가 남긴 한 provenance/rejection 원인의 borrowed view다. */
typedef struct ConfitV5ReasonView {
  ConfitV5ReasonKind kind;
  int satisfied;
  const char *subject;
  const char *cause;
  ConfitV5SourceSpan source;
} ConfitV5ReasonView;

/**
 * @brief repository에서 정확히 한 ARCH-scoped KERNCONF를 읽는 요청이다.
 *
 * 문자열은 호출자가 load 반환까지 유지한다. ARCH는 read-only discovery
 * context이며 Config 문서가 target tuple로 다시 정의할 수 없다.
 */
typedef struct ConfitV5CatalogRequest {
  const char *repository_root;
  const char *architecture;
  const char *kernconf;
} ConfitV5CatalogRequest;

typedef struct ConfitV5Catalog ConfitV5Catalog;
typedef struct ConfitV5Evaluation ConfitV5Evaluation;

/** @brief bounded discovery와 strict schema 검사를 거쳐 catalog를 소유해 반환한다. */
ConfitStatus confit_v5_catalog_load(const ConfitV5CatalogRequest *request,
                                    ConfitV5Catalog **out_catalog,
                                    ConfitDiagnostic *diagnostic);
/** @brief catalog와 그 안의 모든 owned metadata를 해제한다. */
void confit_v5_catalog_free(ConfitV5Catalog *catalog);

/** @brief catalog에 봉인된 external ARCH atom을 빌려 반환한다. */
const char *confit_v5_catalog_architecture(const ConfitV5Catalog *catalog);
/** @brief catalog에 봉인된 KERNCONF 이름을 빌려 반환한다. */
const char *confit_v5_catalog_kernconf(const ConfitV5Catalog *catalog);
/** @brief membership에 참여한 입력 문서 수를 반환한다. */
size_t confit_v5_catalog_document_count(const ConfitV5Catalog *catalog);
/** @brief 선택 가능한 option 수를 반환한다. */
size_t confit_v5_catalog_option_count(const ConfitV5Catalog *catalog);
/** @brief TUI menu 수를 반환한다. */
size_t confit_v5_catalog_menu_count(const ConfitV5Catalog *catalog);
/** @brief choice constraint 수를 반환한다. */
size_t confit_v5_catalog_choice_count(const ConfitV5Catalog *catalog);
/** @brief 전역 implication rule 수를 반환한다. */
size_t confit_v5_catalog_rule_count(const ConfitV5Catalog *catalog);
/** @brief KERNCONF의 명시적 single-writer assignment 수를 반환한다. */
size_t confit_v5_catalog_assignment_count(const ConfitV5Catalog *catalog);
/** @brief index의 KERNCONF assignment를 borrowed view로 반환한다. */
int confit_v5_catalog_assignment(const ConfitV5Catalog *catalog, size_t index,
                                 ConfitV5Assignment *out_assignment);
/** @brief symbol의 option metadata를 borrowed view로 반환한다. */
int confit_v5_catalog_option(const ConfitV5Catalog *catalog,
                             const char *symbol,
                             ConfitV5OptionView *out_option);
/** @brief lexical symbol order의 option을 borrowed view로 반환한다. */
int confit_v5_catalog_option_at(const ConfitV5Catalog *catalog, size_t index,
                                ConfitV5OptionView *out_option);
/** @brief stable menu order의 menu를 borrowed view로 반환한다. */
int confit_v5_catalog_menu_at(const ConfitV5Catalog *catalog, size_t index,
                              ConfitV5MenuView *out_menu);

/** @brief caller assignment를 default 위에 한 번 적용해 immutable 평가를 만든다. */
ConfitStatus confit_v5_evaluate(const ConfitV5Catalog *catalog,
                                const ConfitV5Assignment *assignments,
                                size_t assignment_count,
                                ConfitV5Evaluation **out_evaluation,
                                ConfitDiagnostic *diagnostic);
/** @brief catalog가 소유한 KERNCONF assignment만으로 평가한다. */
ConfitStatus confit_v5_evaluate_kernconf(
    const ConfitV5Catalog *catalog, ConfitV5Evaluation **out_evaluation,
    ConfitDiagnostic *diagnostic);
/** @brief 평가 값과 provenance reason을 모두 해제한다. */
void confit_v5_evaluation_free(ConfitV5Evaluation *evaluation);
/** @brief symbol의 effective textual value를 평가 lifetime 동안 빌려준다. */
const char *confit_v5_evaluation_value(const ConfitV5Evaluation *evaluation,
                                       const char *symbol);
/** @brief effective value 수를 반환한다. */
size_t confit_v5_evaluation_value_count(const ConfitV5Evaluation *evaluation);
/** @brief 정렬된 effective value 한 항목을 borrowed view로 반환한다. */
int confit_v5_evaluation_value_at(const ConfitV5Evaluation *evaluation,
                                  size_t index, const char **out_symbol,
                                  const char **out_value, int *out_enabled,
                                  ConfitV5SourceSpan *out_source);
/** @brief 평가 provenance/rejection reason 수를 반환한다. */
size_t confit_v5_evaluation_reason_count(
    const ConfitV5Evaluation *evaluation);
/** @brief index의 reason을 borrowed view로 반환한다. */
int confit_v5_evaluation_reason(const ConfitV5Evaluation *evaluation,
                                size_t index,
                                ConfitV5ReasonView *out_reason);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_CONFIG_V5_H */
