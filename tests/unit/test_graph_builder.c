#include <string.h>

#include "confit/diagnostic.h"
#include "confit/graph.h"
#include "confit/host.h"
#include "confit/schema.h"
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

static int expect_graph_error(const char *fixture, const char *message) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  char path[512];

  if (!join_fixture(path, sizeof(path), fixture)) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  if (confit_schema_load_project(path, &project, &diagnostic) != CONFIT_OK) {
    return 0;
  }

  graph = 0;
  if (confit_graph_build(project, &graph, &diagnostic) !=
      CONFIT_ERR_DEPENDENCY) {
    confit_graph_free(graph);
    confit_project_free(project);
    return 0;
  }
  confit_project_free(project);

  return confit_diagnostic_has_error(&diagnostic) &&
         diagnostic.message != 0 && strcmp(diagnostic.message, message) == 0;
}

static int expect_graph_validation_error(const char *fixture,
                                         ConfitStatus expected_status,
                                         const char *message) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitStatus status;
  int matched;
  char path[512];

  if (!join_fixture(path, sizeof(path), fixture)) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  if (confit_schema_load_project(path, &project, &diagnostic) != CONFIT_OK) {
    return 0;
  }

  graph = 0;
  if (confit_graph_build(project, &graph, &diagnostic) != CONFIT_OK) {
    confit_project_free(project);
    return 0;
  }
  confit_project_free(project);

  status = confit_graph_validate(graph, &diagnostic);
  confit_graph_free(graph);
  matched = status == expected_status &&
            confit_diagnostic_has_error(&diagnostic) &&
            diagnostic.message != 0 &&
            strcmp(diagnostic.message, message) == 0;
  return matched;
}

static int expect_large_graph(void) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  char path[512];
  int ok;

  if (!join_fixture(path, sizeof(path), "tests/fixtures/graph/valid/large")) {
    return 0;
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  if (confit_schema_load_project(path, &project, &diagnostic) != CONFIT_OK) {
    return 0;
  }

  graph = 0;
  if (confit_graph_build(project, &graph, &diagnostic) != CONFIT_OK) {
    confit_project_free(project);
    return 0;
  }
  confit_project_free(project);

  ok = graph->node_count == 40U && graph->edge_count == 39U &&
       confit_graph_validate(graph, &diagnostic) == CONFIT_OK;
  confit_graph_free(graph);
  return ok;
}

static size_t count_edges(const ConfitGraph *graph, ConfitDependencyKind kind) {
  size_t index;
  size_t count;

  count = 0U;
  for (index = 0U; index < graph->edge_count; ++index) {
    if (graph->edges[index].kind == kind) {
      count += 1U;
    }
  }
  return count;
}

int main(void) {
  ConfitDiagnostic diagnostic;
  ConfitProject *project;
  ConfitGraph *graph;
  char *json;
  char path[512];

  if (!join_fixture(path, sizeof(path),
                    "tests/fixtures/schema/valid/basic")) {
    return 1;
  }

  confit_diagnostic_init(&diagnostic);
  project = 0;
  if (confit_schema_load_project(path, &project, &diagnostic) != CONFIT_OK) {
    return 2;
  }

  graph = 0;
  if (confit_graph_build(project, &graph, &diagnostic) != CONFIT_OK) {
    confit_project_free(project);
    return 3;
  }
  confit_project_free(project);

  if (graph->node_count != 8U || graph->edge_count != 5U) {
    confit_graph_free(graph);
    return 4;
  }
  if (count_edges(graph, CONFIT_DEPENDENCY_REQUIRES) != 1U ||
      count_edges(graph, CONFIT_DEPENDENCY_CONFLICTS) != 1U ||
      count_edges(graph, CONFIT_DEPENDENCY_RECOMMENDS) != 1U ||
      count_edges(graph, CONFIT_DEPENDENCY_FORCES) != 1U ||
      count_edges(graph, CONFIT_DEPENDENCY_VISIBLE_IF) != 1U) {
    confit_graph_free(graph);
    return 5;
  }
  if (confit_graph_validate(graph, &diagnostic) != CONFIT_OK) {
    confit_graph_free(graph);
    return 6;
  }

  json = 0;
  if (confit_graph_to_json(graph, &json) != CONFIT_OK) {
    confit_graph_free(graph);
    return 7;
  }
  if (strstr(json, "\"schema\": \"confit-graph-v1\"") == 0 ||
      strstr(json, "\"project\": \"delos\"") == 0 ||
      strstr(json, "\"id\": \"delos.debug.ddc\"") == 0 ||
      strstr(json, "\"kind\": \"forces\"") == 0 ||
      strstr(json, "\"kind\": \"visible_if\"") == 0) {
    confit_graph_string_free(json);
    confit_graph_free(graph);
    return 8;
  }
  confit_graph_string_free(json);
  confit_graph_free(graph);

  if (!expect_graph_error(
          "tests/fixtures/graph/invalid/unknown-dependency",
          "unknown dependency option")) {
    return 9;
  }
  if (!expect_graph_error("tests/fixtures/graph/invalid/self-edge",
                          "dependency self-edge")) {
    return 10;
  }
  if (!expect_graph_validation_error("tests/fixtures/graph/invalid/cycle",
                                     CONFIT_ERR_DEPENDENCY,
                                     "dependency cycle")) {
    return 11;
  }
  if (!expect_graph_validation_error(
          "tests/fixtures/graph/invalid/forces-visible",
          CONFIT_ERR_DEPENDENCY, "forces visible option")) {
    return 12;
  }
  if (!expect_graph_validation_error(
          "tests/fixtures/graph/invalid/forces-non-internal",
          CONFIT_ERR_DEPENDENCY, "forces non-internal option")) {
    return 13;
  }
  if (!expect_graph_validation_error(
          "tests/fixtures/graph/invalid/forces-target-requires",
          CONFIT_ERR_DEPENDENCY, "forces option with requirements")) {
    return 14;
  }
  if (!expect_graph_validation_error(
          "tests/fixtures/graph/invalid/conflict-contradiction",
          CONFIT_ERR_CONFLICT, "conflict contradiction")) {
    return 15;
  }
  if (!expect_large_graph()) {
    return 16;
  }

  return 0;
}
