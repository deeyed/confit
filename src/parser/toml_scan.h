#ifndef CONFIT_TOML_SCAN_H
#define CONFIT_TOML_SCAN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Tiny TOML syntax scanner for Confit's parser adapter.
 *
 * This is first-party parser support code. It intentionally lives outside
 * vendor/ because vendor/ is reserved for third-party headers or libraries.
 */

typedef struct ConfitTomlScanDocument ConfitTomlScanDocument;

typedef struct ConfitTomlScanError {
  size_t line;
  size_t column;
  const char *message;
} ConfitTomlScanError;

int confit_toml_scan_parse(const char *source, size_t source_size,
                             ConfitTomlScanDocument **out_document,
                             ConfitTomlScanError *error);

void confit_toml_scan_free(ConfitTomlScanDocument *document);

size_t confit_toml_scan_line_count(const ConfitTomlScanDocument *document);
size_t confit_toml_scan_table_count(const ConfitTomlScanDocument *document);
size_t confit_toml_scan_key_count(const ConfitTomlScanDocument *document);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_TOML_SCAN_H */
