#include <stddef.h>
#include <string.h>

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/status.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

int main(void) {
  ConfitDiagnostic diagnostic;
  char fixture_path[512];
  char joined_path[64];
  char *text;
  size_t text_size;
  char **paths;
  size_t path_count;

  confit_diagnostic_init(&diagnostic);

  if (confit_host_path_separator() == '\0') {
    return 1;
  }
#if defined(_WIN32)
  if (confit_host_path_separator() != '\\') {
    return 19;
  }
#else
  if (confit_host_path_separator() != '/') {
    return 19;
  }
#endif

  if (confit_host_path_join(joined_path, sizeof(joined_path), "left",
                            "right", &diagnostic) != CONFIT_OK) {
    return 2;
  }
  if (strcmp(joined_path, "left/right") != 0 &&
      strcmp(joined_path, "left\\right") != 0) {
    return 3;
  }

  if (confit_host_path_join(joined_path, sizeof(joined_path), "left/", "right",
                            &diagnostic) != CONFIT_OK) {
    return 4;
  }
  if (strcmp(joined_path, "left/right") != 0) {
    return 5;
  }

  if (confit_host_path_join(joined_path, sizeof(joined_path), "left\\",
                            "right", &diagnostic) != CONFIT_OK) {
    return 20;
  }
  if (strcmp(joined_path, "left\\right") != 0) {
    return 21;
  }

  if (confit_host_path_join(joined_path, 4U, "left", "right",
                            &diagnostic) != CONFIT_ERR_INVALID_ARGUMENT) {
    return 6;
  }
  if (!confit_diagnostic_has_error(&diagnostic)) {
    return 7;
  }

  confit_diagnostic_clear(&diagnostic);
  if (confit_host_path_join(fixture_path, sizeof(fixture_path),
                            CONFIT_TEST_SOURCE_DIR,
                            "tests/fixtures/host/read_text.txt",
                            &diagnostic) != CONFIT_OK) {
    return 8;
  }

  text = 0;
  text_size = 0U;
  if (confit_host_read_text_file(fixture_path, &text, &text_size,
                                 &diagnostic) != CONFIT_OK) {
    return 9;
  }
  if (text_size != 20U) {
    confit_host_free(text);
    return 10;
  }
  if (strcmp(text, "confit host fixture\n") != 0) {
    confit_host_free(text);
    return 11;
  }
  confit_host_free(text);

  if (confit_host_read_text_file("", &text, &text_size,
                                 &diagnostic) !=
      CONFIT_ERR_INVALID_ARGUMENT) {
    return 12;
  }

  if (confit_host_path_join(fixture_path, sizeof(fixture_path),
                            CONFIT_TEST_SOURCE_DIR,
                            "tests/fixtures/host/list", &diagnostic) !=
      CONFIT_OK) {
    return 13;
  }
  paths = 0;
  path_count = 0U;
  if (confit_host_list_toml_files(fixture_path, &paths, &path_count,
                                  &diagnostic) != CONFIT_OK) {
    return 14;
  }
  if (path_count != 2U) {
    confit_host_string_list_free(paths, path_count);
    return 15;
  }
  if (strstr(paths[0], "a.toml") == 0 || strstr(paths[1], "b.toml") == 0) {
    confit_host_string_list_free(paths, path_count);
    return 16;
  }
  confit_host_string_list_free(paths, path_count);

  if (confit_host_path_join(fixture_path, sizeof(fixture_path),
                            CONFIT_TEST_SOURCE_DIR,
                            "tests/fixtures/host/missing", &diagnostic) !=
      CONFIT_OK) {
    return 17;
  }
  paths = 0;
  path_count = 99U;
  if (confit_host_list_toml_files(fixture_path, &paths, &path_count,
                                  &diagnostic) != CONFIT_OK ||
      paths != 0 || path_count != 0U) {
    confit_host_string_list_free(paths, path_count);
    return 18;
  }

  return 0;
}
