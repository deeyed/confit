#include "confit/parser.h"

#include <stdlib.h>
#include <string.h>

#include "confit/host.h"
#include "toml_scan.h"

struct ConfitParserDocument {
  char *source_name;
  char *source_text;
  size_t source_size;
  ConfitTomlScanDocument *toml;
};

static char *confit_parser_copy_bytes(const char *text, size_t size) {
  char *copy;

  copy = (char *)malloc(size + 1U);
  if (copy == 0) {
    return 0;
  }

  if (size > 0U) {
    memcpy(copy, text, size);
  }
  copy[size] = '\0';
  return copy;
}

static char *confit_parser_copy_string(const char *text) {
  if (text == 0) {
    return 0;
  }

  return confit_parser_copy_bytes(text, strlen(text));
}

static ConfitStatus confit_parser_make_document(
    const char *source_name, char *owned_text, size_t text_size,
    ConfitTomlScanDocument *toml, ConfitParserDocument **out_document,
    ConfitDiagnostic *diagnostic) {
  ConfitParserDocument *document;

  document = (ConfitParserDocument *)calloc(1U, sizeof(*document));
  if (document == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, source_name, 0, 0,
                          "failed to allocate parser document");
    return CONFIT_ERR_INTERNAL;
  }

  document->source_name = confit_parser_copy_string(source_name);
  if (source_name != 0 && document->source_name == 0) {
    free(document);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, source_name, 0, 0,
                          "failed to allocate parser source name");
    return CONFIT_ERR_INTERNAL;
  }

  document->source_text = owned_text;
  document->source_size = text_size;
  document->toml = toml;
  *out_document = document;
  return CONFIT_OK;
}

ConfitStatus confit_parser_load_text(const char *source_name, const char *text,
                                     size_t text_size,
                                     ConfitParserDocument **out_document,
                                     ConfitDiagnostic *diagnostic) {
  char *owned_text;
  ConfitTomlScanDocument *toml;
  ConfitTomlScanError error;
  ConfitStatus status;

  if (out_document == 0 || text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, source_name,
                          0, 0, "invalid parser argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  *out_document = 0;
  owned_text = confit_parser_copy_bytes(text, text_size);
  if (owned_text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, source_name, 0, 0,
                          "failed to allocate parser source text");
    return CONFIT_ERR_INTERNAL;
  }

  error.line = 0U;
  error.column = 0U;
  error.message = 0;
  toml = 0;
  if (!confit_toml_scan_parse(owned_text, text_size, &toml, &error)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_PARSE, source_name, error.line,
                          error.column, error.message);
    free(owned_text);
    return CONFIT_ERR_PARSE;
  }

  status = confit_parser_make_document(source_name, owned_text, text_size, toml,
                                       out_document, diagnostic);
  if (status != CONFIT_OK) {
    confit_toml_scan_free(toml);
    free(owned_text);
    return status;
  }

  return CONFIT_OK;
}

ConfitStatus confit_parser_load_file(const char *path,
                                     ConfitParserDocument **out_document,
                                     ConfitDiagnostic *diagnostic) {
  char *text;
  char *owned_text;
  size_t text_size;
  ConfitTomlScanDocument *toml;
  ConfitTomlScanError error;
  ConfitStatus status;

  if (out_document == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, path, 0, 0,
                          "missing parser document output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  *out_document = 0;
  text = 0;
  text_size = 0U;
  status = confit_host_read_text_file(path, &text, &text_size, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  error.line = 0U;
  error.column = 0U;
  error.message = 0;
  toml = 0;
  if (!confit_toml_scan_parse(text, text_size, &toml, &error)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_PARSE, path, error.line,
                          error.column, error.message);
    confit_host_free(text);
    return CONFIT_ERR_PARSE;
  }

  owned_text = confit_parser_copy_bytes(text, text_size);
  confit_host_free(text);
  if (owned_text == 0) {
    confit_toml_scan_free(toml);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, path, 0, 0,
                          "failed to allocate parser source text");
    return CONFIT_ERR_INTERNAL;
  }

  status = confit_parser_make_document(path, owned_text, text_size, toml,
                                       out_document, diagnostic);
  if (status != CONFIT_OK) {
    confit_toml_scan_free(toml);
    free(owned_text);
    return status;
  }

  return CONFIT_OK;
}

void confit_parser_document_free(ConfitParserDocument *document) {
  if (document == 0) {
    return;
  }

  free(document->source_name);
  free(document->source_text);
  confit_toml_scan_free(document->toml);
  free(document);
}

const char *
confit_parser_document_source_name(const ConfitParserDocument *document) {
  return document != 0 ? document->source_name : 0;
}

const char *
confit_parser_document_source_text(const ConfitParserDocument *document) {
  return document != 0 ? document->source_text : 0;
}

size_t
confit_parser_document_source_size(const ConfitParserDocument *document) {
  return document != 0 ? document->source_size : 0U;
}

size_t
confit_parser_document_line_count(const ConfitParserDocument *document) {
  return document != 0 ? confit_toml_scan_line_count(document->toml) : 0U;
}

size_t
confit_parser_document_table_count(const ConfitParserDocument *document) {
  return document != 0 ? confit_toml_scan_table_count(document->toml) : 0U;
}

size_t confit_parser_document_key_count(const ConfitParserDocument *document) {
  return document != 0 ? confit_toml_scan_key_count(document->toml) : 0U;
}
