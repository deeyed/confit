#ifndef CONFIT_COMPONENT_CATALOG_H
#define CONFIT_COMPONENT_CATALOG_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/schema_v2.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief component.toml v3의 closed selectable kind다. */
typedef enum ConfitComponentKind {
  CONFIT_COMPONENT_KIND_INVALID = 0,
  CONFIT_COMPONENT_KIND_KERNEL_FEATURE,
  CONFIT_COMPONENT_KIND_KERNEL_PROVIDER,
  CONFIT_COMPONENT_KIND_WORLD_FEATURE,
  CONFIT_COMPONENT_KIND_WORLD_SERVICE,
} ConfitComponentKind;

/** @brief manifest atom의 exact source position이다. */
typedef struct ConfitComponentSourceSpan {
  size_t line;
  size_t column;
} ConfitComponentSourceSpan;

/** @brief one strict component.toml v3 manifest와 sibling Makefile의 owner record다. */
typedef struct ConfitComponent {
  char *id;
  ConfitComponentKind kind;
  char *summary;
  char *owner;
  char *manifest_path;
  char *makefile_path;
  char *build_include;
  char **sources;
  size_t source_count;
  char **public_headers;
  size_t public_header_count;
  char **feature_requires;
  ConfitComponentSourceSpan *feature_requirement_spans;
  size_t feature_requirement_count;
  char **feature_provides;
  ConfitComponentSourceSpan *feature_provide_spans;
  size_t feature_provide_count;
  char **feature_conflicts;
  ConfitComponentSourceSpan *feature_conflict_spans;
  size_t feature_conflict_count;
  char **kapi_requires;
  ConfitComponentSourceSpan *kapi_requirement_spans;
  size_t kapi_requirement_count;
  char **kapi_provides;
  ConfitComponentSourceSpan *kapi_provide_spans;
  size_t kapi_provide_count;
} ConfitComponent;

/** @brief fixed project roots에서 얻은 complete available v3 catalog다. */
typedef struct ConfitComponentCatalog {
  char *project_root;
  ConfitComponent *components;
  size_t component_count;
} ConfitComponentCatalog;

/** @brief profile이 ambiguous feature에 지정한 exact provider다. */
typedef struct ConfitComponentProviderChoice {
  const char *feature;
  const char *component_id;
  const char *source_path;
  size_t source_line;
  size_t source_column;
} ConfitComponentProviderChoice;

/** @brief provider 선택이 catalog uniqueness인지 profile explicit mapping인지 구분한다. */
typedef enum ConfitComponentProviderSelection {
  CONFIT_COMPONENT_PROVIDER_SELECTION_NONE = 0,
  CONFIT_COMPONENT_PROVIDER_SELECTION_UNIQUE,
  CONFIT_COMPONENT_PROVIDER_SELECTION_EXPLICIT,
} ConfitComponentProviderSelection;

/** @brief immutable feature/KAPI reason graph의 edge kind다. */
typedef enum ConfitComponentReasonKind {
  CONFIT_COMPONENT_REASON_ROOT_FEATURE = 1,
  CONFIT_COMPONENT_REASON_FEATURE_REQUIREMENT,
  CONFIT_COMPONENT_REASON_KAPI_REQUIREMENT,
} ConfitComponentReasonKind;

/** @brief profile root에서 selected component까지의 exact authority edge다. */
typedef struct ConfitComponentReason {
  ConfitComponentReasonKind kind;
  ConfitComponentProviderSelection provider_selection;
  char *component_id;
  char *from_id;
  char *requirement;
  char *source_path;
  size_t source_line;
  size_t source_column;
} ConfitComponentReason;

/** @brief dependency-first deterministic v3 selection closure다. */
typedef struct ConfitComponentClosure {
  char **root_features;
  size_t root_feature_count;
  char **absent_optional_features;
  size_t absent_optional_feature_count;
  const ConfitComponent **ordered;
  size_t component_count;
  ConfitComponentReason *reasons;
  size_t reason_count;
  char **kapi_requires;
  size_t kapi_requirement_count;
  char **kapi_provides;
  size_t kapi_provide_count;
} ConfitComponentClosure;

/** @brief closed kind의 stable schema spelling을 반환한다. */
const char *confit_component_kind_name(ConfitComponentKind kind);

/** @brief provider selection spelling을 반환한다. */
const char *confit_component_provider_selection_name(
    ConfitComponentProviderSelection selection);

/** @brief project component_roots를 bounded scan하여 v3-only catalog를 만든다. */
ConfitStatus confit_component_catalog_load(const ConfitV2Project *project,
                                           ConfitComponentCatalog *out_catalog,
                                           ConfitDiagnostic *diagnostic);

/** @brief catalog allocation을 해제한다. */
void confit_component_catalog_clear(ConfitComponentCatalog *catalog);

/**
 * @brief root feature와 explicit provider mapping을 deterministic closure로 해석한다.
 *
 * Required feature는 explicit mapping 또는 catalog의 유일 candidate로만 결정한다.
 * Optional feature는 provider가 없으면 absent record로 남고, 둘 이상이면 required와
 * 동일하게 ambiguity failure다. Component ID를 production root로 받지 않는다.
 */
ConfitStatus confit_component_catalog_resolve_features(
    const ConfitComponentCatalog *catalog,
    const char *const *required_features, size_t required_feature_count,
    const char *const *optional_features, size_t optional_feature_count,
    const ConfitComponentProviderChoice *provider_choices,
    size_t provider_choice_count, ConfitComponentClosure *out_closure,
    ConfitDiagnostic *diagnostic);

/**
 * @brief exact schema v3 profile file의 feature/provider intent를 해석한다.
 *
 * Profile grammar는 top-level `schema_version`, `[profile]`, `[features]`,
 * `[providers]`만 허용한다. Transitive component ID, runtime grant와 output path는
 * vocabulary에 없으며 symlinked profile은 거부한다.
 */
ConfitStatus confit_component_catalog_resolve_profile_file(
    const ConfitComponentCatalog *catalog, const char *profile_path,
    ConfitComponentClosure *out_closure, ConfitDiagnostic *diagnostic);

/**
 * @brief one component의 transitive feature dependency를 diagnostic view로 해석한다.
 *
 * 이 API는 deps/rdeps/explain용이며 component ID를 production profile root로 승격하지
 * 않는다. Ambiguous dependency는 explicit provider choice가 없으면 실패한다.
 */
ConfitStatus confit_component_catalog_resolve_component_diagnostic(
    const ConfitComponentCatalog *catalog, const char *component_id,
    const ConfitComponentProviderChoice *provider_choices,
    size_t provider_choice_count, ConfitComponentClosure *out_closure,
    ConfitDiagnostic *diagnostic);

/** @brief closure allocation을 해제한다. */
void confit_component_closure_clear(ConfitComponentClosure *closure);

/** @brief exact stable component ID lookup이다. */
const ConfitComponent *confit_component_catalog_find(
    const ConfitComponentCatalog *catalog, const char *id);

/** @brief feature의 candidate를 lexical ID order로 반환한다. */
size_t confit_component_catalog_find_feature_providers(
    const ConfitComponentCatalog *catalog, const char *feature,
    const ConfitComponent **out_candidates, size_t capacity);

/** @brief selected closure 안의 exact KAPI provider를 반환한다. */
const ConfitComponent *confit_component_closure_find_kapi_provider(
    const ConfitComponentClosure *closure, const char *kapi);

/** @brief stable diagnostic spelling을 반환한다. */
const char *confit_component_reason_kind_name(ConfitComponentReasonKind kind);

/** @brief bounded typo candidate를 lexical tie-break로 반환한다. */
size_t confit_component_catalog_suggest(const ConfitComponentCatalog *catalog,
                                        const char *id,
                                        const ConfitComponent **out_candidates,
                                        size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_COMPONENT_CATALOG_H */
