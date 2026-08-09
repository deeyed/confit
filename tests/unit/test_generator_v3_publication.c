#include <stdio.h>
#include <string.h>

#include "confit/constraint_v2.h"
#include "confit/diagnostic.h"
#include "confit/generator_v2.h"
#include "confit/host.h"
#include "confit/link_v2.h"
#include "confit/resolver_v2.h"
#include "confit/schema_v2.h"
#include "confit/status.h"
#include "test_assert.h"
#include "test_fs.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

static ConfitStatus load_snapshot(ConfitV2Snapshot **out_snapshot,
                                  ConfitDiagnostic *diagnostic) {
  ConfitV2Project *project = 0;
  ConfitV2LinkedProject *linked = 0;
  ConfitV2CompiledStructure *compiled = 0;
  char fixture[1024];
  ConfitStatus status;

  *out_snapshot = 0;
  status = confit_host_path_join(fixture, sizeof(fixture),
                                 CONFIT_TEST_SOURCE_DIR,
                                 "tests/fixtures/schema-v2/valid", diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_schema_load_project(fixture, &project, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_schema_link_project(project, &linked, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_compile_structure(linked, &compiled, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_snapshot_resolve(compiled, 0, out_snapshot, diagnostic);
  }
  confit_v2_compiled_structure_free(compiled);
  confit_v2_linked_project_free(linked);
  confit_v2_project_free(project);
  return status;
}

int main(void) {
  ConfitDiagnostic diagnostic;
  ConfitV2Snapshot *snapshot = 0;
  ConfitV3ArtifactSet artifacts;
  ConfitV3ArtifactSet incomplete;
  ConfitV3PublishOptions publication;
  char root[1024];
  char selected_config[1200];
  char generation_config[1200];
  char partial_config[1200];
  char generation_relative[96];
  char *selected_text;
  char *generation_text;
  size_t changed = 0U;

  confit_diagnostic_init(&diagnostic);
  memset(&artifacts, 0, sizeof(artifacts));
  memset(&publication, 0, sizeof(publication));
  CONFIT_TEST_ASSERT(load_snapshot(&snapshot, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(snapshot != 0);
  CONFIT_TEST_ASSERT(confit_v3_generate_artifacts(
                         snapshot, 0, &artifacts, &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(confit_test_fs_make_temp_dir(
      root, sizeof(root), "confit-publish"));
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      selected_config, sizeof(selected_config), root, "selected/config.mk"));
  CONFIT_TEST_ASSERT(snprintf(generation_relative,
                              sizeof(generation_relative),
                              "generations/%s/config.mk",
                              artifacts.bundle_digest) > 0);
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      generation_config, sizeof(generation_config), root, generation_relative));
  CONFIT_TEST_ASSERT(snprintf(generation_relative,
                              sizeof(generation_relative),
                              ".staging/%s.0000.staging/config.h",
                              artifacts.bundle_digest) > 0);
  CONFIT_TEST_ASSERT(confit_test_fs_path_join(
      partial_config, sizeof(partial_config), root, generation_relative));

  publication.output_root = root;
  publication.fault_after_artifact = 3U;
  CONFIT_TEST_ASSERT(confit_v3_publish_artifacts(
                         &publication, &artifacts, &changed,
                         &diagnostic) == CONFIT_ERR_GENERATION);
  CONFIT_TEST_ASSERT(!confit_test_fs_file_exists(selected_config));
  CONFIT_TEST_ASSERT(!confit_test_fs_file_exists(generation_config));
  CONFIT_TEST_ASSERT(!confit_test_fs_file_exists(partial_config));

  confit_diagnostic_clear(&diagnostic);
  publication.fault_after_artifact = 0U;
  changed = 0U;
  CONFIT_TEST_ASSERT(confit_v3_publish_artifacts(
                         &publication, &artifacts, &changed,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(changed == 1U);
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(selected_config));
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(generation_config));
  selected_text = confit_test_fs_read_file(selected_config);
  generation_text = confit_test_fs_read_file(generation_config);
  CONFIT_TEST_ASSERT(selected_text != 0 && generation_text != 0);
  CONFIT_TEST_ASSERT(strcmp(selected_text, generation_text) == 0);
  confit_test_fs_free(selected_text);
  confit_test_fs_free(generation_text);

  changed = 99U;
  CONFIT_TEST_ASSERT(confit_v3_publish_artifacts(
                         &publication, &artifacts, &changed,
                         &diagnostic) == CONFIT_OK);
  CONFIT_TEST_ASSERT(changed == 0U);

  incomplete = artifacts;
  incomplete.config_header = 0;
  CONFIT_TEST_ASSERT(confit_v3_publish_artifacts(
                         &publication, &incomplete, &changed,
                         &diagnostic) == CONFIT_ERR_INVALID_ARGUMENT);
  CONFIT_TEST_ASSERT(confit_test_fs_file_exists(selected_config));

  confit_v3_artifact_set_clear(&artifacts);
  confit_v2_snapshot_free(snapshot);
  CONFIT_TEST_ASSERT(confit_test_fs_remove_tree(root));
  return 0;
}
