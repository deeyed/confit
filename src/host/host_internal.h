#ifndef CONFIT_HOST_INTERNAL_H
#define CONFIT_HOST_INTERNAL_H

#include <stddef.h>

#include "confit/host.h"

typedef struct ConfitHostDirectoryTransaction
    ConfitHostDirectoryTransaction;

/*
 * Internal multi-file directory transaction primitive.  It is intentionally
 * absent from the public host API: snapshot policy owns its only product use.
 * The implementation never enumerates a directory and cleanup removes only
 * leaf names created and recorded by the transaction.
 */
ConfitStatus confit_host_directory_transaction_begin(
    ConfitHostRoot *root, const char *parent_path,
    ConfitHostDirectoryTransaction **out_transaction,
    ConfitDiagnostic *diagnostic);

const char *confit_host_directory_transaction_relative_path(
    const ConfitHostDirectoryTransaction *transaction);

ConfitStatus confit_host_directory_transaction_write(
    ConfitHostDirectoryTransaction *transaction, const char *leaf,
    const void *bytes, size_t size, unsigned permissions,
    ConfitDiagnostic *diagnostic);

ConfitStatus confit_host_directory_transaction_seal(
    ConfitHostDirectoryTransaction *transaction, unsigned permissions,
    ConfitDiagnostic *diagnostic);

/*
 * Publish the sealed candidate without replacing an existing directory.
 * `out_created` is one when the candidate acquired the final name and zero
 * when an existing directory already owns that name.  In the latter case the
 * caller must verify that existing directory before activating it.
 */
ConfitStatus confit_host_directory_transaction_publish(
    ConfitHostDirectoryTransaction *transaction, const char *final_leaf,
    int *out_created, ConfitDiagnostic *diagnostic);

void confit_host_directory_transaction_destroy(
    ConfitHostDirectoryTransaction *transaction);

#endif /* CONFIT_HOST_INTERNAL_H */
