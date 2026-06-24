#include <stddef.h>
#include <string.h>

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/parser.h"
#include "confit/status.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

static int join_fixture(char *out, size_t out_size, const char *fixture) {
  ConfitDiagnostic diagnostic;

  confit_diagnostic_init(&diagnostic);
  return confit_host_path_join(out, out_size, CONFIT_TEST_SOURCE_DIR, fixture,
                               &diagnostic) == CONFIT_OK;
}

int main(void) {
  static const char crlf_source[] =
      "[project]\r\n"
      "name = \"delos\"\r\n"
      "schema_version = 1\r\n"
      "[values]\r\n"
      "\"delos.path\" = \"C:\\\\Delos SDK\\\\config\"\r\n";
  ConfitDiagnostic diagnostic;
  ConfitParserDocument *document;
  char path[512];

  confit_diagnostic_init(&diagnostic);
  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/toml/valid/project.toml")) {
    return 1;
  }

  document = 0;
  if (confit_parser_load_file(path, &document, &diagnostic) != CONFIT_OK) {
    return 2;
  }
  if (confit_parser_document_table_count(document) != 2U) {
    confit_parser_document_free(document);
    return 3;
  }
  if (confit_parser_document_key_count(document) != 11U) {
    confit_parser_document_free(document);
    return 4;
  }
  if (confit_parser_document_line_count(document) == 0U) {
    confit_parser_document_free(document);
    return 5;
  }
  if (confit_parser_document_source_size(document) == 0U) {
    confit_parser_document_free(document);
    return 6;
  }
  confit_parser_document_free(document);

  document = 0;
  if (confit_parser_load_text("inline-valid", "[profile]\nname = \"debug\"\n",
                              strlen("[profile]\nname = \"debug\"\n"),
                              &document, &diagnostic) != CONFIT_OK) {
    return 7;
  }
  if (strcmp(confit_parser_document_source_name(document), "inline-valid") !=
      0) {
    confit_parser_document_free(document);
    return 8;
  }
  if (confit_parser_document_table_count(document) != 1U ||
      confit_parser_document_key_count(document) != 1U) {
    confit_parser_document_free(document);
    return 9;
  }
  confit_parser_document_free(document);

  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/toml/invalid/unclosed-string.toml")) {
    return 10;
  }
  document = 0;
  confit_diagnostic_clear(&diagnostic);
  if (confit_parser_load_file(path, &document, &diagnostic) !=
      CONFIT_ERR_PARSE) {
    return 11;
  }
  if (!confit_diagnostic_has_error(&diagnostic) || diagnostic.line != 2U ||
      diagnostic.column == 0U) {
    return 12;
  }

  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/toml/invalid/missing-equals.toml")) {
    return 13;
  }
  document = 0;
  confit_diagnostic_clear(&diagnostic);
  if (confit_parser_load_file(path, &document, &diagnostic) !=
      CONFIT_ERR_PARSE) {
    return 14;
  }
  if (diagnostic.line != 3U || diagnostic.column == 0U) {
    return 15;
  }

  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/toml/invalid/unterminated-array.toml")) {
    return 16;
  }
  document = 0;
  confit_diagnostic_clear(&diagnostic);
  if (confit_parser_load_file(path, &document, &diagnostic) !=
      CONFIT_ERR_PARSE) {
    return 17;
  }
  if (diagnostic.line != 2U || diagnostic.column == 0U) {
    return 18;
  }

  if (confit_parser_load_text("inline-invalid", "bad = \"\\q\"\n",
                              strlen("bad = \"\\q\"\n"), &document,
                              &diagnostic) != CONFIT_ERR_PARSE) {
    return 19;
  }

  document = 0;
  confit_diagnostic_clear(&diagnostic);
  if (confit_parser_load_text("inline-crlf", crlf_source,
                              strlen(crlf_source), &document,
                              &diagnostic) != CONFIT_OK) {
    return 20;
  }
  if (confit_parser_document_table_count(document) != 2U ||
      confit_parser_document_key_count(document) != 3U ||
      confit_parser_document_line_count(document) != 5U) {
    confit_parser_document_free(document);
    return 21;
  }
  confit_parser_document_free(document);

  return 0;
}
