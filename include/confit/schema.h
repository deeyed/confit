#ifndef CONFIT_SCHEMA_H
#define CONFIT_SCHEMA_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/model.h"
#include "confit/source.h"
#include "confit/status.h"
#include "confit/toml.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque owner of one structurally validated schema 6 project. */
typedef struct ConfitSchemaProject ConfitSchemaProject;

/**
 * @brief Borrowed R08 declaration view before type-domain validation.
 *
 * `default_candidate`, `values_candidate`, and `range_candidate` are the exact
 * read-only TOML nodes retained by the project's owned input graph.  R09 owns
 * their type-specific interpretation.  A null pointer means the optional field
 * was omitted.
 */
typedef struct ConfitSchemaConfigView {
  size_t fragment;
  size_t menu;
  const char *symbol;
  const char *type_name;
  const char *prompt;
  const char *help;
  const char *dependency_text;
  const ConfitTomlValue *default_candidate;
  const ConfitTomlValue *values_candidate;
  const ConfitTomlValue *range_candidate;
  ConfitSourceSpan declaration;
} ConfitSchemaConfigView;

/**
 * @brief Load the explicit source graph and validate schema 6 structure.
 *
 * The loader follows no paths beyond the R07 literal source graph.  It
 * validates the closed entry/menu/config key sets and constructs presentation
 * fragments and menus, while retaining raw type candidates for R09.  Failure
 * publishes no partial project.
 */
ConfitStatus confit_schema_project_load(
    ConfitHostRoot *project_root, const char *entry_path,
    const ConfitAllocator *allocator, ConfitSchemaProject **out_project,
    ConfitDiagnostic *diagnostic);

void confit_schema_project_destroy(ConfitSchemaProject *project);

const ConfitSourceGraph *
confit_schema_project_source_graph(const ConfitSchemaProject *project);
const ConfitCatalog *
confit_schema_project_catalog(const ConfitSchemaProject *project);
size_t confit_schema_project_config_count(const ConfitSchemaProject *project);
int confit_schema_project_config_at(const ConfitSchemaProject *project,
                                    size_t index,
                                    ConfitSchemaConfigView *out_view);
int confit_schema_project_find_config(const ConfitSchemaProject *project,
                                      const char *symbol,
                                      ConfitSchemaConfigView *out_view);

/** @brief Opaque owner of one structurally validated user configuration. */
typedef struct ConfitUserDocument ConfitUserDocument;

typedef struct ConfitUserValueView {
  const char *symbol;
  const ConfitTomlValue *value_candidate;
  ConfitSourceSpan declaration;
} ConfitUserValueView;

/**
 * @brief Load an explicitly named project-root-relative user TOML document.
 *
 * R08 validates only `schema_version` and optional `[values]` structure.  R13
 * later links symbols and interprets native scalar types.  No file is searched
 * or inferred when the explicit path is absent.
 */
ConfitStatus confit_user_document_load_relative(
    ConfitHostRoot *project_root, const char *path,
    const ConfitAllocator *allocator, ConfitUserDocument **out_document,
    ConfitDiagnostic *diagnostic);

void confit_user_document_destroy(ConfitUserDocument *document);
const ConfitInputImage *
confit_user_document_input(const ConfitUserDocument *document);
size_t confit_user_document_value_count(const ConfitUserDocument *document);
int confit_user_document_value_at(const ConfitUserDocument *document,
                                  size_t index,
                                  ConfitUserValueView *out_view);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_SCHEMA_H */
