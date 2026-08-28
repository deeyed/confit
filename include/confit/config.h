#ifndef CONFIT_CONFIG_H
#define CONFIT_CONFIG_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/input.h"
#include "confit/model.h"
#include "confit/resolver.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque owner of one linked, typed schema 6 user configuration. */
typedef struct ConfitUserConfig ConfitUserConfig;

/**
 * @brief Load and link an explicitly named user configuration.
 *
 * The file is read exactly once beneath `project_root`.  Its closed
 * `schema_version = 6` plus optional `[values]` structure is validated, every
 * symbol is linked to `catalog`, and every native TOML scalar is converted to
 * the declaration's exact type.  Assignments are owned in lexical symbol
 * order.  Unknown, stale, duplicate, or type-invalid values fail without
 * publishing a partial configuration.
 */
ConfitStatus confit_user_config_load_relative(
    ConfitHostRoot *project_root, const char *path,
    const ConfitCatalog *catalog, const ConfitAllocator *allocator,
    ConfitUserConfig **out_config, ConfitDiagnostic *diagnostic);

/** @brief Load and link one explicitly named absolute user configuration. */
ConfitStatus confit_user_config_load_absolute(
    const char *absolute_path, const ConfitCatalog *catalog,
    const ConfitAllocator *allocator, ConfitUserConfig **out_config,
    ConfitDiagnostic *diagnostic);

void confit_user_config_destroy(ConfitUserConfig *config);

/** @brief Borrow the exact input image owned by the user configuration. */
const ConfitInputImage *
confit_user_config_input(const ConfitUserConfig *config);

/** @brief Number of explicit assignments, excluding declaration defaults. */
size_t confit_user_config_assignment_count(const ConfitUserConfig *config);

/** @brief Borrow one assignment in lexical symbol order. */
int confit_user_config_assignment_at(const ConfitUserConfig *config,
                                     size_t lexical_index,
                                     const ConfitAssignment **out_assignment);

/**
 * @brief Borrow the contiguous unordered-set input accepted by the resolver.
 *
 * The returned array remains valid until `config` is destroyed.  A zero count
 * may return null.  Its lexical order is deterministic but creates no value
 * precedence.
 */
const ConfitAssignment *
confit_user_config_assignments(const ConfitUserConfig *config,
                               size_t *out_count);

/**
 * @brief Format one successful resolution as minimal canonical user TOML.
 *
 * Only values with user origin that differ from their declaration defaults
 * are emitted.  Output is lexical by symbol and always contains the stable
 * `[values]` table, even when empty.  Passing a null buffer with size zero is
 * a size query and succeeds.  `out_size` excludes the trailing NUL.  An
 * undersized non-null buffer is left untouched.  Output that would exceed the
 * public one-file TOML ceiling is rejected before publication.
 */
ConfitStatus confit_user_config_format_minimal(
    const ConfitResolution *resolution, char *buffer, size_t buffer_size,
    size_t *out_size, ConfitDiagnostic *diagnostic);

/**
 * @brief Explicitly write minimal user TOML through the atomic host primitive.
 *
 * This is the only R13 save operation.  Loading, resolving, and in-memory
 * formatting never modify an input file.  The destination is an explicit
 * normalized TOML path beneath `destination_root`.  Publication is one atomic
 * old-or-complete-new replacement; no partial user file is exposed.
 */
ConfitStatus confit_user_config_write_minimal(
    ConfitHostRoot *destination_root, const char *destination_path,
    const ConfitResolution *resolution, const ConfitAllocator *allocator,
    ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_CONFIG_H */
