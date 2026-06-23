#include "confit/tui.h"

#include "tui_internal.h"

ConfitStatus confit_tui_run(const ConfitTuiOptions *options,
                            ConfitDiagnostic *diagnostic) {
  if (options == 0 || options->project_root == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid tui options");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (options->schema_edit) {
    return confit_tui_run_schema_editor(options, diagnostic);
  }
  if (options->profile_name == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid tui options");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  return confit_tui_run_profile_editor(options, diagnostic);
}
