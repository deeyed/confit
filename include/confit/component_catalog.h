#ifndef CONFIT_COMPONENT_CATALOG_H
#define CONFIT_COMPONENT_CATALOG_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/schema_v2.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief component.toml v1의 closed build kind다. */
typedef enum ConfitComponentKind {
  CONFIT_COMPONENT_KIND_INVALID = 0,
  CONFIT_COMPONENT_KIND_KERNEL_CORE,
  CONFIT_COMPONENT_KIND_KERNEL_DRIVER,
  CONFIT_COMPONENT_KIND_USER_LIBRARY,
  CONFIT_COMPONENT_KIND_USER_SERVICE,
  CONFIT_COMPONENT_KIND_HOST_TOOL,
  CONFIT_COMPONENT_KIND_TARGET_IMAGE,
  CONFIT_COMPONENT_KIND_TEST,
} ConfitComponentKind;

/** @brief one strict component.toml manifest의 immutable catalog record다. */
typedef struct ConfitComponent {
  char *id;
  ConfitComponentKind kind;
  char *manifest_path;
  char *makefile_path;
  char **enabled_if;
  size_t enabled_if_count;
  char **component_dependencies;
  size_t component_dependency_count;
  char **kapi_requires;
  size_t kapi_requirement_count;
  char **capabilities;
  size_t capability_count;
  char **kapi_provides;
  size_t kapi_provide_count;
} ConfitComponent;

/** @brief fixed project roots에서 얻은 complete available catalog다. */
typedef struct ConfitComponentCatalog {
  char *project_root;
  ConfitComponent *components;
  size_t component_count;
} ConfitComponentCatalog;

/** @brief selected root의 dependency-first deterministic closure다. */
typedef struct ConfitComponentClosure {
  char **root_ids;
  size_t root_count;
  const ConfitComponent **ordered;
  size_t component_count;
} ConfitComponentClosure;

/** @brief closed kind의 stable schema spelling을 반환한다. */
const char *confit_component_kind_name(ConfitComponentKind kind);

/** @brief project component_roots를 bounded scan하여 complete catalog를 만든다. */
ConfitStatus confit_component_catalog_load(const ConfitV2Project *project,
                                           ConfitComponentCatalog *out_catalog,
                                           ConfitDiagnostic *diagnostic);

/** @brief catalog allocation을 해제한다. */
void confit_component_catalog_clear(ConfitComponentCatalog *catalog);

/** @brief exact root set을 dependency-first deterministic closure로 해석한다. */
ConfitStatus confit_component_catalog_resolve(
    const ConfitComponentCatalog *catalog, const char *const *root_ids,
    size_t root_count, ConfitComponentClosure *out_closure,
    ConfitDiagnostic *diagnostic);

/**
 * @brief component root와 required/optional capability request를 하나의 closed selection으로 해석한다.
 *
 * Required capability는 정확히 한 catalog provider가 있어야 하며, 없으면 fail-closed한다.
 * Optional capability는 provider가 없을 때 root를 추가하지 않는다. Provider가 있으면 해당 component를
 * effective root로 추가한다. 모든 effective root는 duplicate 없이 기존 dependency closure 규칙으로
 * 해석된다. Returned closure의 root_ids는 caller가 요청한 capability의 provider까지 포함한다.
 */
ConfitStatus confit_component_catalog_resolve_selection(
    const ConfitComponentCatalog *catalog, const char *const *component_roots,
    size_t component_root_count, const char *const *required_capabilities,
    size_t required_capability_count, const char *const *optional_capabilities,
    size_t optional_capability_count, ConfitComponentClosure *out_closure,
    ConfitDiagnostic *diagnostic);

/** @brief closure allocation을 해제한다. */
void confit_component_closure_clear(ConfitComponentClosure *closure);

/** @brief exact stable component ID lookup이다. */
const ConfitComponent *confit_component_catalog_find(
    const ConfitComponentCatalog *catalog, const char *id);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_COMPONENT_CATALOG_H */
