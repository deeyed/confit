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

/** @brief closure allocation을 해제한다. */
void confit_component_closure_clear(ConfitComponentClosure *closure);

/** @brief exact stable component ID lookup이다. */
const ConfitComponent *confit_component_catalog_find(
    const ConfitComponentCatalog *catalog, const char *id);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_COMPONENT_CATALOG_H */
