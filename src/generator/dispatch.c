#include "confit/generator_v2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/host.h"

static ConfitStatus confit_v2_write_one(const char *output_root,
                                        const char *relative_path,
                                        const char *text, size_t *changed_count,
                                        ConfitDiagnostic *diagnostic) {
  char path[4096];
  char directory[4096];
  char *separator;
  int changed;
  ConfitStatus status;

  status = confit_host_path_join(path, sizeof(path), output_root, relative_path,
                                 diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (strlen(path) >= sizeof(directory)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, path, 0, 0,
                          "generated artifact path is too long");
    return CONFIT_ERR_GENERATION;
  }
  memcpy(directory, path, strlen(path) + 1U);
  separator = strrchr(directory, '/');
  {
    char *backslash = strrchr(directory, '\\');
    if (backslash != 0 && (separator == 0 || backslash > separator)) {
      separator = backslash;
    }
  }
  if (separator != 0) {
    *separator = '\0';
    status = confit_host_make_directories(directory, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  status = confit_host_write_text_file_if_changed_atomic(path, text, &changed,
                                                          diagnostic);
  if (status == CONFIT_OK && changed != 0) {
    *changed_count += 1U;
  }
  return status;
}

ConfitStatus confit_v2_write_artifacts(
    const char *output_root, const ConfitV2ArtifactSet *artifacts,
    size_t *out_changed_file_count, ConfitDiagnostic *diagnostic) {
  static const char *const paths[] = {
      "config.h",          "config.report.json", "config.explain.txt",
      "config.graph.json", "config.inputs.json",  "config.changes.json",
      "config.cmake",      "config/config.qsm"};
  const char *texts[sizeof(paths) / sizeof(paths[0])];
  char selection_path[512];
  size_t index;
  size_t changed_count = 0U;
  ConfitStatus status;

  if (output_root == 0 || output_root[0] == '\0' || artifacts == 0 ||
      artifacts->selection_name == 0 || artifacts->config_header == 0 ||
      artifacts->report_json == 0 || artifacts->explain_text == 0 ||
      artifacts->graph_json == 0 || artifacts->inputs_json == 0 ||
      artifacts->changes_json == 0 || artifacts->cmake_fragment == 0 ||
      artifacts->qsm_module == 0 || artifacts->selection_module == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, output_root,
                          0, 0, "invalid schema v2 artifact bundle");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  {
    const int selection_path_size =
        snprintf(selection_path, sizeof(selection_path), "%s/%s.qsm",
                 artifacts->selection_name, artifacts->selection_name);
    if (selection_path_size < 0 ||
        (size_t)selection_path_size >= sizeof(selection_path)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, output_root, 0,
                            0, "build selection artifact path is too long");
      return CONFIT_ERR_GENERATION;
    }
  }
  texts[0] = artifacts->config_header;
  texts[1] = artifacts->report_json;
  texts[2] = artifacts->explain_text;
  texts[3] = artifacts->graph_json;
  texts[4] = artifacts->inputs_json;
  texts[5] = artifacts->changes_json;
  texts[6] = artifacts->cmake_fragment;
  texts[7] = artifacts->qsm_module;
  for (index = 0U; index < sizeof(paths) / sizeof(paths[0]); ++index) {
    status = confit_v2_write_one(output_root, paths[index], texts[index],
                                 &changed_count, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  status = confit_v2_write_one(output_root, selection_path,
                               artifacts->selection_module, &changed_count,
                               diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (out_changed_file_count != 0) {
    *out_changed_file_count = changed_count;
  }
  return CONFIT_OK;
}
