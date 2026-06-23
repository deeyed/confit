#include "confit/tui.h"

ConfitStatus confit_tui_run(const ConfitTuiOptions *options,
                            ConfitDiagnostic *diagnostic) {
  const char *path;

  path = options != 0 ? options->project_root : 0;
  confit_diagnostic_set(
      diagnostic, CONFIT_ERR_UNSUPPORTED, path, 0, 0,
      "confit tui is unsupported in this CLI-only platform lane");
  return CONFIT_ERR_UNSUPPORTED;
}
