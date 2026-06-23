#include <string.h>

#include "confit/diagnostic.h"
#include "confit/status.h"
#include "confit/version.h"

int main(void) {
  ConfitDiagnostic diagnostic;

  if (!confit_status_is_ok(CONFIT_OK)) {
    return 1;
  }
  if (confit_status_exit_code(CONFIT_ERR_SCHEMA) != 3) {
    return 2;
  }
  if (strcmp(confit_status_name(CONFIT_ERR_CONFLICT),
             "dependency or conflict error") != 0) {
    return 3;
  }
  if (strcmp(confit_version_string(), "confit 0.1.0-round1") != 0) {
    return 4;
  }

  confit_diagnostic_init(&diagnostic);
  if (confit_diagnostic_has_error(&diagnostic)) {
    return 5;
  }

  confit_diagnostic_set(&diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                        "config/project.toml", 7, 3, "invalid project name");
  if (!confit_diagnostic_has_error(&diagnostic)) {
    return 6;
  }
  if (diagnostic.status != CONFIT_ERR_INVALID_ARGUMENT) {
    return 7;
  }
  if (diagnostic.line != 7 || diagnostic.column != 3) {
    return 8;
  }
  if (strcmp(diagnostic.path, "config/project.toml") != 0) {
    return 9;
  }
  if (strcmp(diagnostic.message, "invalid project name") != 0) {
    return 10;
  }

  confit_diagnostic_clear(&diagnostic);
  if (confit_diagnostic_has_error(&diagnostic)) {
    return 11;
  }

  return 0;
}
