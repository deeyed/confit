#ifndef CONFIT_SOURCE_CATALOG_H
#define CONFIT_SOURCE_CATALOG_H

#include <stddef.h>
#include <stdint.h>

#include "confit/component_catalog.h"
#include "confit/diagnostic.h"
#include "confit/schema_v2.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief mandatory kernel leaf 하나의 immutable source-owner record다. */
typedef struct ConfitNucleusUnit {
  char *id;
  char *directory;
  char *makefile_path;
  char **sources;
  size_t source_count;
  char **uses;
  size_t use_count;
  char **kapi_exports;
  size_t kapi_export_count;
  char **public_headers;
  size_t public_header_count;
} ConfitNucleusUnit;

/** @brief hierarchy-owned mandatory kernel source graph다. */
typedef struct ConfitNucleusCatalog {
  char *project_root;
  ConfitNucleusUnit *units;
  size_t unit_count;
} ConfitNucleusCatalog;

/** @brief owner-local restricted test Makefile 하나의 typed record다. */
typedef struct ConfitTestUnit {
  char *id;
  char *owner;
  char *lane;
  char *evidence_class;
  uint32_t timeout_ms;
  char *target;
  char *machine_profile;
  char *receipt_profile;
  char *directory;
  char *makefile_path;
  char **sources;
  size_t source_count;
} ConfitTestUnit;

/** @brief central ID registry 없이 local Makefile에서 얻은 complete test catalog다. */
typedef struct ConfitTestCatalog {
  char *project_root;
  ConfitTestUnit *tests;
  size_t test_count;
} ConfitTestCatalog;

/** @brief project의 declared nucleus roots를 bounded traversal하여 검증한다. */
ConfitStatus confit_nucleus_catalog_load(const ConfitV2Project *project,
                                         ConfitNucleusCatalog *out_catalog,
                                         ConfitDiagnostic *diagnostic);

/** @brief nucleus catalog allocation을 해제한다. */
void confit_nucleus_catalog_clear(ConfitNucleusCatalog *catalog);

/** @brief project의 test roots에서 restricted local test Makefile을 발견한다. */
ConfitStatus confit_test_catalog_load(const ConfitV2Project *project,
                                      ConfitTestCatalog *out_catalog,
                                      ConfitDiagnostic *diagnostic);

/** @brief test owner를 exact nucleus/selectable/target identity에 결속한다. */
ConfitStatus confit_test_catalog_validate_owners(
    const ConfitTestCatalog *tests, const ConfitNucleusCatalog *nucleus,
    const ConfitComponentCatalog *components, ConfitDiagnostic *diagnostic);

/** @brief test catalog allocation을 해제한다. */
void confit_test_catalog_clear(ConfitTestCatalog *catalog);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_SOURCE_CATALOG_H */
