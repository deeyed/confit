#ifndef CONFIT_MIGRATION_INTERNAL_H
#define CONFIT_MIGRATION_INTERNAL_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/model.h"
#include "confit/status.h"

ConfitStatus confit_migration_catalog_summary_format(
    const ConfitCatalog *catalog, const ConfitAllocator *allocator,
    unsigned char **out_bytes, size_t *out_size,
    ConfitDiagnostic *diagnostic);

#endif /* CONFIT_MIGRATION_INTERNAL_H */
