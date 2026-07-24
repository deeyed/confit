#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "confit/constraint_v2.h"
#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/link_v2.h"
#include "confit/resolver_v2.h"
#include "confit/schema_v2.h"
#include "confit/status.h"

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

static int has_provenance_kind(const ConfitV2Snapshot *snapshot,
                               ConfitV2ProvenanceKind kind) {
  size_t index;

  for (index = 0U;
       index < confit_v2_snapshot_provenance_node_count(snapshot); ++index) {
    const ConfitV2ProvenanceNode *node =
        confit_v2_snapshot_provenance_node_at(snapshot, index);
    if (node != 0 && node->kind == kind) {
      return 1;
    }
  }
  return 0;
}

static int invalidation_contains(const ConfitV2InvalidationSet *set,
                                 ConfitV2InvalidationKind kind,
                                 const char *id) {
  size_t index;

  for (index = 0U; index < confit_v2_invalidation_set_count(set); ++index) {
    const ConfitV2InvalidationNode *node =
        confit_v2_invalidation_set_at(set, index);
    if (node != 0 && node->kind == kind && strcmp(node->id, id) == 0) {
      return 1;
    }
  }
  return 0;
}

static int expect_snapshot_lifetime_and_provenance(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2Snapshot *snapshot = 0;
  ConfitV2LedgerOptions options;
  const ConfitV2SnapshotOption *requested;
  const ConfitV2SnapshotOption *total;
  ConfitDiagnostic diagnostic;
  uint64_t semantic_hash;
  uint64_t source_hash;
  uint64_t input_hash;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (load_compiled("tests/fixtures/schema-v2-evaluation/valid", &project,
                    &linked, &compiled, &diagnostic) != CONFIT_OK) {
    return 0;
  }
  memset(&options, 0, sizeof(options));
  options.profile_name = "debug";
  result = confit_v2_snapshot_resolve(compiled, &options, &snapshot,
                                      &diagnostic) == CONFIT_OK &&
           snapshot != 0 && confit_v2_snapshot_option_count(snapshot) == 4U &&
           confit_v2_snapshot_choice_count(snapshot) == 0U &&
           confit_v2_snapshot_constraint_count(snapshot) == 0U;
  requested = confit_v2_snapshot_find_option(snapshot, "eval.requested");
  total = confit_v2_snapshot_find_option(snapshot, "eval.total");
  result = result && requested != 0 && requested->requested.is_present &&
           requested->write_domain == CONFIT_V2_WRITE_DOMAIN_PROFILE &&
           requested->requested.origin == CONFIT_V2_ASSIGNMENT_ORIGIN_PROFILE &&
           requested->requested.is_set &&
           requested->requested.value.kind == CONFIT_V2_VALUE_UINT &&
           requested->requested.value.as.uint_value == 4U && total != 0 &&
           total->effective_origin == CONFIT_V2_EFFECTIVE_VALUE_COMPUTED &&
           total->effective_value.kind == CONFIT_V2_VALUE_UINT &&
           total->effective_value.as.uint_value == 11U &&
           has_provenance_kind(snapshot,
                               CONFIT_V2_PROVENANCE_PROFILE_ASSIGNMENT) &&
           has_provenance_kind(snapshot,
                               CONFIT_V2_PROVENANCE_CONDITIONAL_DEFAULT) &&
           has_provenance_kind(snapshot, CONFIT_V2_PROVENANCE_COMPUTED) &&
           confit_v2_snapshot_provenance_edge_count(snapshot) > 0U;
  semantic_hash = confit_v2_snapshot_semantic_hash(snapshot);
  source_hash = confit_v2_snapshot_source_hash(snapshot);
  input_hash = confit_v2_snapshot_input_hash(snapshot);
  free_compiled(project, linked, compiled);
  result = result && semantic_hash != 0U && source_hash != 0U && input_hash != 0U &&
           confit_v2_snapshot_find_option(snapshot, "eval.total") != 0 &&
           confit_v2_snapshot_find_option(snapshot, "eval.total")
                   ->effective_value.as.uint_value == 11U;
  confit_v2_snapshot_free(snapshot);
  return result;
}

static int expect_invalidation_and_reconcile(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2Snapshot *base = 0;
  ConfitV2Snapshot *incremental = 0;
  ConfitV2Snapshot *full = 0;
  ConfitV2InvalidationSet *affected = 0;
  ConfitV2InvalidationSet *missing = 0;
  ConfitV2ProfileOverride override;
  ConfitV2LedgerOptions options;
  ConfitDiagnostic diagnostic;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (load_compiled("tests/fixtures/schema-v2-evaluation/valid", &project,
                    &linked, &compiled, &diagnostic) != CONFIT_OK) {
    return 0;
  }
  memset(&options, 0, sizeof(options));
  options.profile_name = "debug";
  result = confit_v2_snapshot_resolve(compiled, &options, &base, &diagnostic) ==
           CONFIT_OK;
  memset(&override, 0, sizeof(override));
  override.option_id = "eval.requested";
  override.value_text = "7";
  options.profile_overrides = &override;
  options.profile_override_count = 1U;
  result = result && confit_v2_snapshot_reconcile_edit(
                         base, compiled, &options, "eval.requested",
                         &incremental, &affected, &diagnostic) == CONFIT_OK &&
           confit_v2_snapshot_resolve(compiled, &options, &full, &diagnostic) ==
               CONFIT_OK &&
           confit_v2_snapshot_semantic_hash(incremental) ==
               confit_v2_snapshot_semantic_hash(full) &&
           confit_v2_snapshot_input_hash(incremental) ==
               confit_v2_snapshot_input_hash(full) &&
           confit_v2_snapshot_source_hash(base) ==
               confit_v2_snapshot_source_hash(full) &&
           confit_v2_snapshot_input_hash(base) !=
               confit_v2_snapshot_input_hash(full) &&
           invalidation_contains(affected, CONFIT_V2_INVALIDATION_OPTION,
                                 "eval.requested") &&
           confit_v2_snapshot_invalidate(base, "missing.option", &missing,
                                         &diagnostic) == CONFIT_ERR_INVALID_ARGUMENT;
  for (size_t index = 0U; result && index < 1000U; ++index) {
    ConfitV2Snapshot *repeated = 0;
    ConfitV2InvalidationSet *repeated_affected = 0;

    override.value_text = (index & 1U) == 0U ? "7" : "8";
    result = confit_v2_snapshot_reconcile_edit(
                 base, compiled, &options, "eval.requested", &repeated,
                 &repeated_affected, &diagnostic) == CONFIT_OK &&
             repeated != 0 && repeated_affected != 0 &&
             invalidation_contains(repeated_affected,
                                   CONFIT_V2_INVALIDATION_OPTION,
                                   "eval.requested");
    confit_v2_invalidation_set_free(repeated_affected);
    confit_v2_snapshot_free(repeated);
  }
  confit_v2_invalidation_set_free(missing);
  confit_v2_invalidation_set_free(affected);
  confit_v2_snapshot_free(full);
  confit_v2_snapshot_free(incremental);
  confit_v2_snapshot_free(base);
  free_compiled(project, linked, compiled);
  return result;
}

static int expect_menu_lookup_scale(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitDiagnostic diagnostic;
  size_t index;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (load_compiled("tests/fixtures/schema-v2/valid", &project, &linked,
                    &compiled, &diagnostic) != CONFIT_OK) {
    return 0;
  }
  result = 1;
  for (index = 0U; index < 100U; ++index) {
    result = result && confit_v2_compiled_structure_find_menu(
                           compiled, index & 1U ? "main" : "main.features") != 0;
  }
  free_compiled(project, linked, compiled);
  return result;
}

static int expect_constraint_publish_gate(void) {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2Snapshot *snapshot = 0;
  ConfitV2LedgerOptions options;
  ConfitDiagnostic diagnostic;
  int result;

  confit_diagnostic_init(&diagnostic);
  if (load_compiled("tests/fixtures/schema-v2-constraint-runtime", &project,
                    &linked, &compiled, &diagnostic) != CONFIT_OK) {
    return 0;
  }
  result = confit_v2_snapshot_resolve(compiled, 0, &snapshot, &diagnostic) ==
               CONFIT_ERR_SCHEMA &&
           snapshot == 0;
  memset(&options, 0, sizeof(options));
  options.profile_name = "pass";
  result = result && confit_v2_snapshot_resolve(compiled, &options, &snapshot,
                                                &diagnostic) == CONFIT_OK &&
           snapshot != 0 && confit_v2_snapshot_constraint_count(snapshot) == 3U;
  confit_v2_snapshot_free(snapshot);
  free_compiled(project, linked, compiled);
  return result;
}

int main(void) {
  if (!expect_snapshot_lifetime_and_provenance()) {
    return 2;
  }
  if (!expect_invalidation_and_reconcile()) {
    return 3;
  }
  if (!expect_constraint_publish_gate()) {
    return 4;
  }
  if (!expect_menu_lookup_scale()) {
    return 5;
  }
  return 0;
}
