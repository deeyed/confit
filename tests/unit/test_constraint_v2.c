#include <string.h>

#include "confit/constraint_v2.h"
#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/link_v2.h"
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

static int expect_compile_error(const char *fixture, const char *message) {
  ConfitV2Project *project = 0;
  ConfitV2LinkedProject *linked = 0;
  ConfitV2CompiledStructure *compiled = 0;
  ConfitDiagnostic diagnostic;
  char path[512];
  int result;

  if (!join_fixture(path, sizeof(path), fixture)) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  result = confit_v2_schema_load_project(path, &project, &diagnostic) ==
               CONFIT_OK &&
           confit_v2_schema_link_project(project, &linked, &diagnostic) ==
               CONFIT_OK &&
           confit_v2_compile_structure(linked, &compiled, &diagnostic) ==
               CONFIT_ERR_SCHEMA &&
           compiled == 0 && diagnostic.message != 0 &&
           strstr(diagnostic.message, message) != 0 && diagnostic.line > 0U;
  confit_v2_compiled_structure_free(compiled);
  confit_v2_linked_project_free(linked);
  confit_v2_project_free(project);
  return result;
}

static int expect_valid_structure(void) {
  ConfitV2Project *project = 0;
  ConfitV2LinkedProject *linked = 0;
  ConfitV2CompiledStructure *compiled = 0;
  const ConfitV2CompiledMenu *main_menu;
  const ConfitV2CompiledMenu *feature_menu;
  const ConfitV2CompiledChoice *choice;
  const ConfitV2CompiledConstraint *constraint;
  const ConfitV2CompiledGraph *evaluation;
  const ConfitV2CompiledGraph *visibility;
  const ConfitV2CompiledGraph *choice_graph;
  const ConfitV2CompiledGraph *constraint_graph;
  ConfitDiagnostic diagnostic;
  char path[512];
  int result;

  if (!join_fixture(path, sizeof(path), "tests/fixtures/schema-v2/valid")) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  result = confit_v2_schema_load_project(path, &project, &diagnostic) ==
               CONFIT_OK &&
           confit_v2_schema_link_project(project, &linked, &diagnostic) ==
               CONFIT_OK &&
           confit_v2_compile_structure(linked, &compiled, &diagnostic) ==
               CONFIT_OK &&
           confit_v2_compiled_structure_source(compiled) == linked &&
           confit_v2_compiled_structure_menu_count(compiled) == 2U &&
           confit_v2_compiled_structure_menu_reference_count(compiled) == 1U &&
           confit_v2_compiled_structure_choice_count(compiled) == 1U &&
           confit_v2_compiled_structure_constraint_count(compiled) == 1U;
  main_menu = confit_v2_compiled_structure_find_menu(compiled, "main");
  feature_menu =
      confit_v2_compiled_structure_find_menu(compiled, "main.features");
  choice = confit_v2_compiled_structure_choice_at(compiled, 0U);
  constraint = confit_v2_compiled_structure_constraint_at(compiled, 0U);
  evaluation = confit_v2_compiled_structure_graph(
      compiled, CONFIT_V2_COMPILED_GRAPH_EVALUATION);
  visibility = confit_v2_compiled_structure_graph(
      compiled, CONFIT_V2_COMPILED_GRAPH_VISIBILITY);
  choice_graph = confit_v2_compiled_structure_graph(
      compiled, CONFIT_V2_COMPILED_GRAPH_CHOICE);
  constraint_graph = confit_v2_compiled_structure_graph(
      compiled, CONFIT_V2_COMPILED_GRAPH_CONSTRAINT);
  result = result && main_menu != 0 && feature_menu != 0 &&
           feature_menu->parent == main_menu && choice != 0 &&
           choice->member_count == 2U && choice->default_count == 1U &&
           constraint != 0 && constraint->when != 0 && constraint->require != 0 &&
           evaluation != 0 && visibility != 0 && choice_graph != 0 &&
           constraint_graph != 0 && evaluation != visibility &&
           visibility != choice_graph && choice_graph != constraint_graph &&
           evaluation->edge_count >= 2U && choice_graph->edge_count >= 2U &&
           constraint_graph->edge_count >= 2U &&
           confit_v2_compiled_structure_menu_reference_at(compiled, 0U) != 0 &&
           confit_v2_compiled_structure_menu_reference_at(compiled, 0U)->symbol !=
               0;
  confit_v2_compiled_structure_free(compiled);
  confit_v2_linked_project_free(linked);
  confit_v2_project_free(project);
  return result;
}

static int expect_large_structure(void) {
  ConfitV2Project *project = 0;
  ConfitV2LinkedProject *linked = 0;
  ConfitV2CompiledStructure *compiled = 0;
  ConfitDiagnostic diagnostic;
  char path[512];
  int result;

  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/schema-v2-structure/large")) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  result = confit_v2_schema_load_project(path, &project, &diagnostic) ==
               CONFIT_OK &&
           confit_v2_schema_link_project(project, &linked, &diagnostic) ==
               CONFIT_OK &&
           confit_v2_compile_structure(linked, &compiled, &diagnostic) ==
               CONFIT_OK &&
           confit_v2_compiled_structure_menu_count(compiled) == 24U &&
           confit_v2_compiled_structure_choice_count(compiled) == 1U;
  confit_v2_compiled_structure_free(compiled);
  confit_v2_linked_project_free(linked);
  confit_v2_project_free(project);
  return result;
}

int main(void) {
  if (!expect_valid_structure()) {
    return 2;
  }
  if (!expect_large_structure()) {
    return 3;
  }
  if (!expect_compile_error(
          "tests/fixtures/schema-v2-structure/menu-missing-parent",
          "schema v2 menu parent is missing")) {
    return 4;
  }
  if (!expect_compile_error(
          "tests/fixtures/schema-v2-structure/menu-cycle",
          "confit.first -> confit.second -> confit.first")) {
    return 5;
  }
  if (!expect_compile_error(
          "tests/fixtures/schema-v2-structure/menu-duplicate-order",
          "schema v2 menu siblings have duplicate order")) {
    return 6;
  }
  if (!expect_compile_error(
          "tests/fixtures/schema-v2-structure/writable-menu-reference",
          "schema v2 menu reference must be read_only")) {
    return 7;
  }
  if (!expect_compile_error(
          "tests/fixtures/schema-v2-structure/choice-member-type",
          "schema v2 choice member type does not match declaration")) {
    return 8;
  }
  if (!expect_compile_error("tests/fixtures/schema-v2-structure/choice-nested",
                            "schema v2 choice member cannot reference another choice")) {
    return 9;
  }
  if (!expect_compile_error(
          "tests/fixtures/schema-v2-structure/choice-default-member",
          "schema v2 choice default references non-member option")) {
    return 10;
  }
  if (!expect_compile_error(
          "tests/fixtures/schema-v2-structure/constraint-empty-message",
          "schema v2 constraint message must not be empty")) {
    return 11;
  }
  return 0;
}
