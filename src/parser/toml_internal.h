#ifndef CONFIT_TOML_INTERNAL_H
#define CONFIT_TOML_INTERNAL_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/status.h"
#include "confit/toml.h"

/*
 * Parse a byte image without copying or owning it.  `text[text_size]` must be
 * a readable NUL sentinel outside the exact byte count.  The caller must keep
 * the bytes and sentinel alive until the returned document is freed.  This is
 * an internal seam used by ConfitInputImage; public callers use the owned
 * input-image API.
 */
ConfitStatus confit_toml_parse_borrowed(
    const char *source_name, const char *text, size_t text_size,
    ConfitTomlDocument **out_document, ConfitDiagnostic *diagnostic);

#endif /* CONFIT_TOML_INTERNAL_H */
