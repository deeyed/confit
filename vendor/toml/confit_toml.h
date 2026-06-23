#ifndef CONFIT_VENDOR_TOML_H
#define CONFIT_VENDOR_TOML_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tiny TOML syntax loader for Confit's early parser adapter.
 *
 * This file is intentionally isolated under vendor/toml/. It is permissively
 * licensed for reuse inside Confit.
 *
 * SPDX-License-Identifier: MIT
 */

typedef struct ConfitTomlVendorDocument ConfitTomlVendorDocument;

typedef struct ConfitTomlVendorError {
  size_t line;
  size_t column;
  const char *message;
} ConfitTomlVendorError;

int confit_toml_vendor_parse(const char *source, size_t source_size,
                             ConfitTomlVendorDocument **out_document,
                             ConfitTomlVendorError *error);

void confit_toml_vendor_free(ConfitTomlVendorDocument *document);

size_t confit_toml_vendor_line_count(const ConfitTomlVendorDocument *document);
size_t confit_toml_vendor_table_count(const ConfitTomlVendorDocument *document);
size_t confit_toml_vendor_key_count(const ConfitTomlVendorDocument *document);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_VENDOR_TOML_H */
