#include "confit/tui.h"

#include "confit/project.h"

#include "tui_internal.h"

ConfitStatus confit_tui_run(const ConfitTuiOptions *options,
                            ConfitDiagnostic *diagnostic) {
  ConfitProjectHandle *project;
  ConfitSchemaVersion version;
  ConfitStatus status;

  if (options == 0 || options->project_root == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid tui options");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  project = 0;
  status = confit_project_load(options->project_root, &project, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  version = confit_project_handle_schema_version(project);
  confit_project_handle_free(project);
  if (version == CONFIT_SCHEMA_VERSION_V2) {
    if (options->schema_edit) {
      return confit_tui_run_schema_editor_v2(options, diagnostic);
    }
    if (options->profile_name == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                            "invalid tui options");
      return CONFIT_ERR_INVALID_ARGUMENT;
    }
    return confit_tui_run_profile_editor_v2(options, diagnostic);
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
