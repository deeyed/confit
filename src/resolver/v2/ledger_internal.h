#ifndef CONFIT_RESOLVER_V2_LEDGER_INTERNAL_H
#define CONFIT_RESOLVER_V2_LEDGER_INTERNAL_H

#include "confit/parser_v2.h"
#include "confit/resolver_v2.h"

typedef enum ConfitV2InputKind {
  CONFIT_V2_INPUT_KIND_PROFILE = 1,
  CONFIT_V2_INPUT_KIND_TARGET,
} ConfitV2InputKind;

typedef struct ConfitV2InputAssignment {
  const ConfitV2Symbol *symbol;
  ConfitV2Value value;
  int is_unset;
  size_t line;
  size_t column;
  size_t declaration_order;
} ConfitV2InputAssignment;

typedef struct ConfitV2InputDocument {
  char *name;
  char *base;
  char *target;
  size_t target_line;
  size_t target_column;
  char *path;
  ConfitV2InputAssignment *assignments;
  size_t assignment_count;
} ConfitV2InputDocument;

typedef struct ConfitV2InputCatalog {
  ConfitV2InputKind kind;
  ConfitV2InputDocument *documents;
  size_t document_count;
} ConfitV2InputCatalog;

struct ConfitV2AssignmentLedger {
  const ConfitV2CompiledStructure *compiled;
  char *profile_name;
  char *target_name;
  ConfitV2TargetSelection target_selection;
  ConfitV2LedgerEntry *entries;
  size_t entry_count;
};

char *confit_v2_ledger_strdup(const char *text);
void confit_v2_ledger_value_clear(ConfitV2Value *value);
ConfitStatus confit_v2_ledger_value_copy(ConfitV2Value *out,
                                          const ConfitV2Value *value);
ConfitStatus confit_v2_ledger_parse_toml_value(
    const ConfitV2Symbol *symbol, const ConfitV2TomlValue *source,
    ConfitV2Value *out, ConfitDiagnostic *diagnostic);
ConfitStatus confit_v2_ledger_parse_user_value(
    const ConfitV2Symbol *symbol, const char *text, ConfitV2Value *out,
    ConfitDiagnostic *diagnostic);
ConfitStatus confit_v2_input_catalog_load(
    const ConfitV2CompiledStructure *compiled, ConfitV2InputKind kind,
    ConfitV2InputCatalog *out_catalog, ConfitDiagnostic *diagnostic);
void confit_v2_input_catalog_clear(ConfitV2InputCatalog *catalog);
const ConfitV2InputDocument *confit_v2_input_catalog_find(
    const ConfitV2InputCatalog *catalog, const char *name);
ConfitStatus confit_v2_input_catalog_build_chain(
    const ConfitV2InputCatalog *catalog, const char *leaf_name,
    const ConfitV2InputDocument ***out_chain, size_t *out_count,
    ConfitDiagnostic *diagnostic);
void confit_v2_input_chain_free(const ConfitV2InputDocument **chain);
void confit_v2_ledger_diagnostic(const char *path, size_t line, size_t column,
                                 ConfitStatus status, const char *message,
                                 ConfitDiagnostic *diagnostic);

#endif /* CONFIT_RESOLVER_V2_LEDGER_INTERNAL_H */
