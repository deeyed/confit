#include <stdint.h>
#include <string.h>

#include "confit/diagnostic.h"
#include "confit/toml.h"
#include "confit/status.h"
#include "test_fs.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

static int join_fixture(char *out, size_t out_size, const char *fixture) {
  return confit_test_fs_path_join(out, out_size, CONFIT_TEST_SOURCE_DIR,
                                  fixture);
}

static int expect_string(const ConfitTomlValue *value, const char *expected) {
  const char *text;
  size_t text_size;

  text = 0;
  text_size = 0U;
  return confit_toml_value_string(value, &text, &text_size) &&
         strlen(expected) == text_size && memcmp(text, expected, text_size) == 0;
}

static int expect_scalar_values(const ConfitTomlValue *root) {
  const ConfitTomlValue *value;
  double ratio;
  int bool_value;
  int64_t count;

  value = confit_toml_table_find(root, "title");
  if (confit_toml_value_type(value) != CONFIT_TOML_VALUE_STRING ||
      !expect_string(value, "Confit TOML adapter")) {
    return 0;
  }
  value = confit_toml_table_find(root, "enabled");
  if (!confit_toml_value_bool(value, &bool_value) || bool_value != 1) {
    return 0;
  }
  value = confit_toml_table_find(root, "count");
  if (!confit_toml_value_int64(value, &count) || count != 42) {
    return 0;
  }
  value = confit_toml_table_find(root, "ratio");
  if (!confit_toml_value_float64(value, &ratio) || ratio != 3.125) {
    return 0;
  }
  value = confit_toml_table_find(root, "when");
  return confit_toml_value_type(value) == CONFIT_TOML_VALUE_DATETIME_TZ &&
         confit_toml_value_line(value) == 5U &&
         confit_toml_value_column(value) != 0U;
}

static int expect_composite_values(const ConfitTomlValue *root) {
  const ConfitTomlValue *array;
  const ConfitTomlValue *inline_table;
  const ConfitTomlValue *project;
  int64_t retries;

  array = confit_toml_table_find(root, "items");
  if (confit_toml_value_type(array) != CONFIT_TOML_VALUE_ARRAY ||
      confit_toml_array_size(array) != 2U ||
      !expect_string(confit_toml_array_at(array, 0U), "one") ||
      !expect_string(confit_toml_array_at(array, 1U), "two")) {
    return 0;
  }

  inline_table = confit_toml_table_find(root, "inline");
  if (confit_toml_value_type(inline_table) != CONFIT_TOML_VALUE_TABLE ||
      !expect_string(confit_toml_table_find(inline_table, "color"),
                     "green") ||
      !confit_toml_value_int64(
          confit_toml_table_find(inline_table, "retries"), &retries) ||
      retries != 3) {
    return 0;
  }

  project = confit_toml_table_find(root, "project");
  if (confit_toml_value_type(project) != CONFIT_TOML_VALUE_TABLE ||
      !expect_string(confit_toml_table_find(project, "name"), "example")) {
    return 0;
  }

  array = confit_toml_table_find(root, "board");
  return confit_toml_value_type(array) == CONFIT_TOML_VALUE_ARRAY &&
         confit_toml_array_size(array) == 2U &&
         expect_string(confit_toml_table_find(
                           confit_toml_array_at(array, 0U), "id"),
                       "host-sim") &&
         expect_string(confit_toml_table_find(
                           confit_toml_array_at(array, 1U), "id"),
                       "qemu-mps2-an500");
}

int main(void) {
  static const char crlf_source[] =
      "[project]\r\n"
      "name = \"example\"\r\n"
      "format_revision = 2\r\n";
  static const char invalid_utf8[] = {'n', 'a', 'm', 'e', ' ', '=', ' ',
                                      '\"', (char)0xFF, '\"', '\n'};
  static const char embedded_nul[] = {'a', ' ', '=', ' ', '1', '\n',
                                      'b', ' ', '=', ' ', 0, '2', '\n'};
  ConfitDiagnostic diagnostic;
  ConfitTomlDocument *document;
  const ConfitTomlValue *root;
  char path[512];
  char *text;

  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/toml/valid/full.toml")) {
    return 1;
  }
  confit_diagnostic_init(&diagnostic);
  document = 0;
  text = confit_test_fs_read_file(path);
  if (text == 0 || confit_toml_parse_text(path, text, strlen(text), &document,
                                           &diagnostic) != CONFIT_OK) {
    confit_test_fs_free(text);
    return 2;
  }
  confit_test_fs_free(text);
  root = confit_toml_document_root(document);
  if (confit_toml_value_type(root) != CONFIT_TOML_VALUE_TABLE ||
      confit_toml_table_size(root) != 9U ||
      confit_toml_document_source_size(document) == 0U ||
      confit_toml_document_source_text(document) == 0 ||
      !expect_scalar_values(root) || !expect_composite_values(root) ||
      confit_toml_table_find(root, "missing") != 0) {
    confit_toml_document_free(document);
    return 3;
  }
  if (confit_toml_value_source(
          confit_toml_table_find(root, "title")) == 0 ||
      strcmp(confit_toml_value_source(
                 confit_toml_table_find(root, "title")),
             path) != 0) {
    confit_toml_document_free(document);
    return 4;
  }
  confit_toml_document_free(document);

  document = 0;
  confit_diagnostic_clear(&diagnostic);
  if (confit_toml_parse_text("memory-crlf", crlf_source,
                                strlen(crlf_source), &document,
                                &diagnostic) != CONFIT_OK ||
      confit_toml_table_find(confit_toml_document_root(document),
                                "project") == 0) {
    confit_toml_document_free(document);
    return 5;
  }
  confit_toml_document_free(document);

  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/toml/invalid/unclosed-string.toml")) {
    return 6;
  }
  document = 0;
  confit_diagnostic_clear(&diagnostic);
  text = confit_test_fs_read_file(path);
  if (text == 0 || confit_toml_parse_text(path, text, strlen(text), &document,
                                           &diagnostic) !=
          CONFIT_ERR_VALIDATION ||
      document != 0 || !confit_diagnostic_has_error(&diagnostic) ||
      diagnostic.line != 3U || diagnostic.column != 1U ||
      diagnostic.message == 0 ||
      strcmp(diagnostic.message, "tomlc17 rejected TOML input") != 0) {
    confit_test_fs_free(text);
    return 7;
  }
  confit_test_fs_free(text);

  document = 0;
  confit_diagnostic_clear(&diagnostic);
  if (confit_toml_parse_text("invalid-utf8", invalid_utf8,
                                sizeof(invalid_utf8), &document,
                                &diagnostic) != CONFIT_ERR_VALIDATION ||
      document != 0 || diagnostic.line != 1U || diagnostic.column != 9U ||
      diagnostic.message == 0 ||
      strcmp(diagnostic.message, "TOML source is not valid UTF-8") != 0) {
    return 8;
  }

  document = 0;
  confit_diagnostic_clear(&diagnostic);
  if (confit_toml_parse_text("embedded-nul", embedded_nul,
                             sizeof(embedded_nul), &document,
                             &diagnostic) != CONFIT_ERR_VALIDATION ||
      document != 0 || diagnostic.line != 2U || diagnostic.column != 5U ||
      diagnostic.message == 0 ||
      strcmp(diagnostic.message,
             "TOML source contains an embedded NUL byte") != 0) {
    return 9;
  }

  return 0;
}
