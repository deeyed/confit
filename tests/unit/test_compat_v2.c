#include <stdint.h>
#include <string.h>

#include "confit/compat_v2.h"
#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/link_v2.h"
#include "confit/resolver_v2.h"
#include "confit/schema_v2.h"
#include "confit/status.h"
#include "test_fs.h"

#ifndef CONFIT_TEST_SOURCE_DIR
#define CONFIT_TEST_SOURCE_DIR "."
#endif

static int join_fixture(char *out, size_t out_size, const char *fixture) {
  ConfitDiagnostic diagnostic;

  confit_diagnostic_init(&diagnostic);
  return confit_host_path_join(out, out_size, CONFIT_TEST_SOURCE_DIR, fixture,
                               &diagnostic) == CONFIT_OK;
}

static ConfitStatus load_compiled(const char *fixture,
                                  ConfitV2Project **out_project,
                                  ConfitV2LinkedProject **out_linked,
                                  ConfitV2CompiledStructure **out_compiled,
                                  ConfitDiagnostic *diagnostic) {
  char path[1024];
  ConfitStatus status;

  *out_project = 0;
  *out_linked = 0;
  *out_compiled = 0;
  if (!join_fixture(path, sizeof(path), fixture)) {
    return CONFIT_ERR_INTERNAL;
  }
  status = confit_v2_schema_load_project(path, out_project, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_schema_link_project(*out_project, out_linked, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_compile_structure(*out_linked, out_compiled, diagnostic);
  }
  if (status != CONFIT_OK) {
    confit_v2_compiled_structure_free(*out_compiled);
    confit_v2_linked_project_free(*out_linked);
    confit_v2_project_free(*out_project);
    *out_project = 0;
    *out_linked = 0;
    *out_compiled = 0;
  }
  return status;
}

static void free_compiled(ConfitV2Project *project,
                          ConfitV2LinkedProject *linked,
                          ConfitV2CompiledStructure *compiled) {
  confit_v2_compiled_structure_free(compiled);
  confit_v2_linked_project_free(linked);
  confit_v2_project_free(project);
}

static ConfitStatus load_snapshot(const char *fixture,
                                  const ConfitV2LedgerOptions *options,
                                  ConfitV2Snapshot **out_snapshot,
                                  ConfitDiagnostic *diagnostic) {
  ConfitV2Project *project = 0;
  ConfitV2LinkedProject *linked = 0;
  ConfitV2CompiledStructure *compiled = 0;
  ConfitStatus status = load_compiled(fixture, &project, &linked, &compiled,
                                      diagnostic);

  *out_snapshot = 0;
  if (status == CONFIT_OK) {
    status = confit_v2_snapshot_resolve(compiled, options, out_snapshot,
                                        diagnostic);
  }
  free_compiled(project, linked, compiled);
  return status;
}

static int golden_fragments_match(const char *text, const char *fixture) {
  char path[1024];
  char *golden;
  char *cursor;
  int result = 1;

  if (!join_fixture(path, sizeof(path), fixture)) {
    return 0;
  }
  golden = confit_test_fs_read_file(path);
  if (golden == 0) {
    return 0;
  }
  cursor = golden;
  while (*cursor != '\0') {
    char *line = cursor;
    char *end = strchr(cursor, '\n');

    if (end != 0) {
      *end = '\0';
      cursor = end + 1;
    } else {
      cursor += strlen(cursor);
    }
    if (line[0] != '\0' && line[0] != '#' && strstr(text, line) == 0) {
      result = 0;
      break;
    }
  }
  confit_test_fs_free(golden);
  return result;
}

static int expect_realish_pass(const ConfitV2CompatSuite *suite,
                               ConfitV2CompatProject *projects) {
  ConfitV2CompatReport *first = 0;
  ConfitV2CompatReport *second = 0;
  ConfitDiagnostic diagnostic;
  char *first_json = 0;
  char *second_json = 0;
  uint64_t first_hash;
  uint64_t second_hash;
  int result;

  confit_diagnostic_init(&diagnostic);
  result = confit_v2_compat_check(suite, projects, 2U, &first, &diagnostic) ==
               CONFIT_OK &&
           first != 0 &&
           confit_v2_compat_report_to_json(first, &first_json) == CONFIT_OK &&
           first_json != 0 &&
           strstr(first_json, "\"schema\": \"confit-compat-report-v2\"") != 0 &&
           strstr(first_json, "\"passed\": 5") != 0;
  first_hash = confit_v2_compat_report_semantic_hash(first);
  confit_diagnostic_init(&diagnostic);
  result = result &&
           confit_v2_compat_check(suite, projects, 2U, &second, &diagnostic) ==
               CONFIT_OK &&
           second != 0 &&
           confit_v2_compat_report_to_json(second, &second_json) == CONFIT_OK &&
           strcmp(first_json, second_json) == 0;
  second_hash = confit_v2_compat_report_semantic_hash(second);
  result = result && first_hash != 0U && first_hash == second_hash;
  confit_v2_compat_string_free(first_json);
  confit_v2_compat_string_free(second_json);
  confit_v2_compat_report_free(first);
  confit_v2_compat_report_free(second);
  return result;
}

static int expect_mixed_schema_rejected(const ConfitV2CompatSuite *suite,
                                        ConfitV2CompatProject *projects) {
  ConfitV2CompatReport *report = 0;
  ConfitDiagnostic diagnostic;
  const unsigned int original = projects[0].schema_version;
  int result;

  projects[0].schema_version = 1U;
  confit_diagnostic_init(&diagnostic);
  result = confit_v2_compat_check(suite, projects, 2U, &report, &diagnostic) ==
               CONFIT_ERR_SCHEMA &&
           report == 0 && confit_diagnostic_has_error(&diagnostic) &&
           diagnostic.message != 0 &&
           strcmp(diagnostic.message,
                  "v1 and v2 snapshots cannot be mixed in compatibility checking") ==
               0;
  projects[0].schema_version = original;
  return result;
}

static int expect_artifact_identity_rejected(const ConfitV2CompatSuite *suite,
                                             ConfitV2CompatProject *projects) {
  ConfitV2CompatReport *report = 0;
  ConfitDiagnostic diagnostic;
  const char *original_abi = projects[0].artifact_abi;
  const uint64_t original_hash = projects[1].expected_snapshot_hash;
  int result;

  projects[0].artifact_abi = "confit-artifact-v1";
  confit_diagnostic_init(&diagnostic);
  result = confit_v2_compat_check(suite, projects, 2U, &report, &diagnostic) ==
               CONFIT_ERR_SCHEMA &&
           report == 0 && diagnostic.message != 0 &&
           strcmp(diagnostic.message,
                  "schema v2 compatibility artifact ABI mismatch") == 0;
  projects[0].artifact_abi = original_abi;
  projects[1].expected_snapshot_hash = 1U;
  confit_diagnostic_init(&diagnostic);
  result = result &&
           confit_v2_compat_check(suite, projects, 2U, &report, &diagnostic) ==
               CONFIT_ERR_SCHEMA &&
           report == 0 && diagnostic.message != 0 &&
           strcmp(diagnostic.message,
                  "schema v2 compatibility snapshot identity mismatch") == 0;
  projects[1].expected_snapshot_hash = original_hash;
  return result;
}

static int expect_causal_failure(const ConfitV2CompatSuite *suite,
                                 ConfitV2CompatProject *projects) {
  ConfitV2CompatReport *report = 0;
  ConfitDiagnostic diagnostic;
  char *json = 0;
  char *text = 0;
  int result;

  confit_diagnostic_init(&diagnostic);
  result = confit_v2_compat_check(suite, projects, 2U, &report, &diagnostic) ==
               CONFIT_ERR_COMPATIBILITY &&
           report != 0 && confit_diagnostic_has_error(&diagnostic) &&
           diagnostic.message != 0 &&
           strcmp(diagnostic.message,
                  "Parus와 Delos의 target CPU가 일치해야 합니다.") == 0 &&
           confit_v2_compat_report_to_json(report, &json) == CONFIT_OK &&
           confit_v2_compat_report_to_text(report, &text) == CONFIT_OK &&
           json != 0 && text != 0 &&
           golden_fragments_match(json, "tests/golden/compat-v2/failure.json") &&
           strstr(text, "parus-delos.cpu: fail") != 0;
  confit_v2_compat_string_free(json);
  confit_v2_compat_string_free(text);
  confit_v2_compat_report_free(report);
  return result;
}

static int expect_forbid_failure(const ConfitV2CompatProject *projects) {
  ConfitV2CompatSuite *suite = 0;
  ConfitV2CompatReport *report = 0;
  ConfitDiagnostic diagnostic;
  char path[1024];
  int result;

  if (!join_fixture(path, sizeof(path), "tests/fixtures/compat-v2/forbid.toml")) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  if (confit_v2_compat_load_file(path, &suite, &diagnostic) != CONFIT_OK) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  result = confit_v2_compat_check(suite, projects, 2U, &report, &diagnostic) ==
               CONFIT_ERR_COMPATIBILITY &&
           report != 0 && diagnostic.message != 0 &&
           strcmp(diagnostic.message,
                  "이 fixture는 enabled Delos transport를 금지합니다.") == 0;
  confit_v2_compat_report_free(report);
  confit_v2_compat_suite_free(suite);
  return result;
}

int main(void) {
  ConfitV2Snapshot *parus = 0;
  ConfitV2Snapshot *delos = 0;
  ConfitV2CompatSuite *suite = 0;
  ConfitV2CompatProject projects[2];
  ConfitV2LedgerOptions options;
  ConfitV2UserOverride override;
  ConfitDiagnostic diagnostic;
  char compat_path[1024];
  int result;

  memset(projects, 0, sizeof(projects));
  memset(&options, 0, sizeof(options));
  memset(&override, 0, sizeof(override));
  confit_diagnostic_init(&diagnostic);
  if (load_snapshot("tests/fixtures/compat-v2/parus", 0, &parus, &diagnostic) !=
          CONFIT_OK ||
      load_snapshot("tests/fixtures/compat-v2/delos", 0, &delos, &diagnostic) !=
          CONFIT_OK ||
      !join_fixture(compat_path, sizeof(compat_path),
                    "tests/fixtures/compat-v2/realish.toml") ||
      confit_v2_compat_load_file(compat_path, &suite, &diagnostic) != CONFIT_OK) {
    confit_v2_compat_suite_free(suite);
    confit_v2_snapshot_free(parus);
    confit_v2_snapshot_free(delos);
    return 1;
  }
  projects[0].alias = "delos";
  projects[0].snapshot = delos;
  projects[0].schema_version = CONFIT_V2_COMPAT_SCHEMA_VERSION;
  projects[0].artifact_abi = CONFIT_V2_COMPAT_ARTIFACT_ABI;
  projects[1].alias = "parus";
  projects[1].snapshot = parus;
  projects[1].schema_version = CONFIT_V2_COMPAT_SCHEMA_VERSION;
  projects[1].artifact_abi = CONFIT_V2_COMPAT_ARTIFACT_ABI;
  result = expect_realish_pass(suite, projects) &&
           expect_mixed_schema_rejected(suite, projects) &&
           expect_artifact_identity_rejected(suite, projects);
  confit_v2_snapshot_free(delos);
  delos = 0;
  override.option_id = "delos.target.cpu";
  override.value_text = "cortex-m4";
  options.user_overrides = &override;
  options.user_override_count = 1U;
  confit_diagnostic_init(&diagnostic);
  if (load_snapshot("tests/fixtures/compat-v2/delos", &options, &delos,
                    &diagnostic) != CONFIT_OK) {
    result = 0;
  } else {
    projects[0].snapshot = delos;
    result = result && expect_causal_failure(suite, projects) &&
             expect_forbid_failure(projects);
  }
  confit_v2_compat_suite_free(suite);
  confit_v2_snapshot_free(parus);
  confit_v2_snapshot_free(delos);

  confit_diagnostic_init(&diagnostic);
  result = result && join_fixture(compat_path, sizeof(compat_path),
                                   "tests/fixtures/compat-v2/invalid-schema.toml") &&
           confit_v2_compat_load_file(compat_path, &suite, &diagnostic) ==
               CONFIT_ERR_SCHEMA &&
           suite == 0;
  return result ? 0 : 2;
}
