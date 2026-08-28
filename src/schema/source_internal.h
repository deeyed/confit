#ifndef CONFIT_SOURCE_INTERNAL_H
#define CONFIT_SOURCE_INTERNAL_H

#include <stddef.h>

#include "confit/limits.h"
#include "confit/source.h"

typedef enum ConfitSourceReadPurpose {
  CONFIT_SOURCE_READ_ENTRY = 1,
  CONFIT_SOURCE_READ_FRAGMENT = 2,
} ConfitSourceReadPurpose;

typedef struct ConfitSourceReadRecord {
  char path[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  ConfitSourceReadPurpose purpose;
  size_t byte_count;
} ConfitSourceReadRecord;

typedef struct ConfitSourceReadLedger {
  ConfitSourceReadRecord *records;
  size_t capacity;
  size_t count;
} ConfitSourceReadLedger;

void confit_source_read_ledger_init(ConfitSourceReadLedger *ledger,
                                    ConfitSourceReadRecord *records,
                                    size_t capacity);

ConfitStatus confit_source_budget_preflight(
    size_t current_nodes, size_t current_edges, size_t source_count,
    ConfitDiagnostic *diagnostic);

ConfitStatus confit_source_graph_load_observed(
    ConfitHostRoot *project_root, const char *entry_path,
    const ConfitAllocator *allocator, ConfitSourceReadLedger *ledger,
    ConfitSourceGraph **out_graph, ConfitDiagnostic *diagnostic);

#endif /* CONFIT_SOURCE_INTERNAL_H */
