#include <stdint.h>
#include <string.h>

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/toml.h"
#include "confit/status.h"

#include "test_fs.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

static uint32_t next_state(uint32_t *state) {
  *state ^= *state << 13U;
  *state ^= *state >> 17U;
  *state ^= *state << 5U;
  return *state;
}

static int parse_one(const char *source_name, const char *text, size_t size) {
  ConfitTomlDocument *document = 0;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;

  confit_diagnostic_init(&diagnostic);
  status = confit_toml_parse_text(source_name, text, size, &document,
                                     &diagnostic);
  if (status == CONFIT_OK) {
    const ConfitTomlValue *root = confit_toml_document_root(document);

    if (document == 0 || root == 0 ||
        confit_toml_value_type(root) != CONFIT_TOML_VALUE_TABLE) {
      confit_toml_document_free(document);
      return 0;
    }
  } else if (document != 0 ||
             (status != CONFIT_ERR_PARSE && status != CONFIT_ERR_INTERNAL)) {
    confit_toml_document_free(document);
    return 0;
  }
  confit_toml_document_free(document);
  return 1;
}

static int parse_seed_file(const char *name) {
  char corpus[1024];
  char path[1024];
  char *text;
  size_t size;
  ConfitDiagnostic diagnostic;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (confit_host_path_join(corpus, sizeof(corpus), CONFIT_TEST_SOURCE_DIR,
                            "tests/fuzz/corpus/toml", &diagnostic) !=
          CONFIT_OK ||
      confit_host_path_join(path, sizeof(path), corpus, name, &diagnostic) !=
          CONFIT_OK) {
    return 0;
  }
  text = confit_test_fs_read_file(path);
  if (text == 0) {
    return 0;
  }
  size = strlen(text);
  result = parse_one(path, text, size);
  confit_test_fs_free(text);
  return result;
}

int main(void) {
  static const char alphabet[] =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-[]{}=,\"'"
      "#\\/ \t\r\n";
  static const char invalid_utf8[] = {'k', ' ', '=', ' ', '=', ' ',
                                      (char)0xc3, '\0'};
  uint32_t state = 0xC0FFEEU;
  size_t index;

  if (!parse_seed_file("valid-basic.toml") ||
      !parse_seed_file("valid-nested.toml") ||
      !parse_seed_file("invalid-unclosed.toml") ||
      !parse_one("toml-invalid-utf8", invalid_utf8,
                 sizeof(invalid_utf8) - 1U)) {
    return 2;
  }
  for (index = 0U; index < 2048U; ++index) {
    char generated[257];
    size_t length = (size_t)(next_state(&state) % 256U);
    size_t cursor;

    for (cursor = 0U; cursor < length; ++cursor) {
      generated[cursor] = alphabet[next_state(&state) % (sizeof(alphabet) - 1U)];
    }
    generated[length] = '\0';
    if (!parse_one("toml-generated", generated, length)) {
      return 3;
    }
  }
  return 0;
}
