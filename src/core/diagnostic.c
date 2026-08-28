#include "confit/diagnostic.h"

#include <string.h>

void confit_diagnostic_init(ConfitDiagnostic *diagnostic) {
  if (diagnostic == 0) {
    return;
  }

  diagnostic->status = CONFIT_OK;
  diagnostic->path = 0;
  diagnostic->line = 0;
  diagnostic->column = 0;
  diagnostic->message = 0;
  diagnostic->code = 0;
  diagnostic->severity = CONFIT_DIAGNOSTIC_SEVERITY_NONE;
  diagnostic->related = 0;
  diagnostic->related_count = 0U;
  diagnostic->fix_candidates = 0;
  diagnostic->fix_candidate_count = 0U;
  diagnostic->stable_path[0] = '\0';
}

void confit_diagnostic_clear(ConfitDiagnostic *diagnostic) {
  confit_diagnostic_init(diagnostic);
}

void confit_diagnostic_set(ConfitDiagnostic *diagnostic, ConfitStatus status,
                           const char *path, size_t line, size_t column,
                           const char *message) {
  confit_diagnostic_set_detail(
      diagnostic, status,
      status == CONFIT_OK ? CONFIT_DIAGNOSTIC_SEVERITY_NONE
                          : CONFIT_DIAGNOSTIC_SEVERITY_ERROR,
      0, path, line, column, message, 0, 0U, 0, 0U);
}

void confit_diagnostic_set_detail(
    ConfitDiagnostic *diagnostic, ConfitStatus status,
    ConfitDiagnosticSeverity severity, const char *code, const char *path,
    size_t line, size_t column, const char *message,
    const ConfitDiagnosticRelatedSpan *related, size_t related_count,
    const ConfitDiagnosticFixCandidate *fix_candidates,
    size_t fix_candidate_count) {
  if (diagnostic == 0) {
    return;
  }

  if (path != diagnostic->stable_path) {
    diagnostic->stable_path[0] = '\0';
  }
  diagnostic->status = status;
  diagnostic->path = path;
  diagnostic->line = line;
  diagnostic->column = column;
  diagnostic->message = message;
  diagnostic->code = code;
  diagnostic->severity = severity;
  diagnostic->related = related;
  diagnostic->related_count = related_count;
  diagnostic->fix_candidates = fix_candidates;
  diagnostic->fix_candidate_count = fix_candidate_count;
}

int confit_diagnostic_has_error(const ConfitDiagnostic *diagnostic) {
  if (diagnostic == 0) {
    return 0;
  }

  return diagnostic->status != CONFIT_OK;
}

int confit_diagnostic_stabilize_path(ConfitDiagnostic *diagnostic) {
  size_t size;
  if (diagnostic == 0 || diagnostic->path == 0 ||
      diagnostic->path == diagnostic->stable_path) {
    return 1;
  }
  for (size = 0U; size <= CONFIT_LIMIT_SOURCE_PATH_BYTES; ++size) {
    if (diagnostic->path[size] == '\0') {
      memcpy(diagnostic->stable_path, diagnostic->path, size + 1U);
      diagnostic->path = diagnostic->stable_path;
      return 1;
    }
  }
  return 0;
}
