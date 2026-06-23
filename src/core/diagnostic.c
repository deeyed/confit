#include "confit/diagnostic.h"

void confit_diagnostic_init(ConfitDiagnostic *diagnostic) {
  if (diagnostic == 0) {
    return;
  }

  diagnostic->status = CONFIT_OK;
  diagnostic->path = 0;
  diagnostic->line = 0;
  diagnostic->column = 0;
  diagnostic->message = 0;
}

void confit_diagnostic_clear(ConfitDiagnostic *diagnostic) {
  confit_diagnostic_init(diagnostic);
}

void confit_diagnostic_set(ConfitDiagnostic *diagnostic, ConfitStatus status,
                           const char *path, size_t line, size_t column,
                           const char *message) {
  if (diagnostic == 0) {
    return;
  }

  diagnostic->status = status;
  diagnostic->path = path;
  diagnostic->line = line;
  diagnostic->column = column;
  diagnostic->message = message;
}

int confit_diagnostic_has_error(const ConfitDiagnostic *diagnostic) {
  if (diagnostic == 0) {
    return 0;
  }

  return diagnostic->status != CONFIT_OK;
}
