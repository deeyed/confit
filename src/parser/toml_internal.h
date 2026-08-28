#ifndef CONFIT_TOML_INTERNAL_H
#define CONFIT_TOML_INTERNAL_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/status.h"
#include "confit/toml.h"

typedef enum ConfitTomlIntegerBase {
  CONFIT_TOML_INTEGER_BASE_UNKNOWN = 0,
  CONFIT_TOML_INTEGER_BASE_BINARY = 2,
  CONFIT_TOML_INTEGER_BASE_OCTAL = 8,
  CONFIT_TOML_INTEGER_BASE_DECIMAL = 10,
  CONFIT_TOML_INTEGER_BASE_HEXADECIMAL = 16,
} ConfitTomlIntegerBase;

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

/*
 * Recover one integer token's lexical base from the same owned byte image.
 * The value must belong to `document`; mismatched document/value handles fail.
 * This is schema plumbing rather than part of the public TOML adapter surface.
 */
int confit_toml_integer_base_from_image(const ConfitTomlDocument *document,
                                        const ConfitTomlValue *value,
                                        ConfitTomlIntegerBase *out_base);

#endif /* CONFIT_TOML_INTERNAL_H */
