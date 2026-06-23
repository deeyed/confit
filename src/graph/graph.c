#include "confit/graph.h"

#include <stdlib.h>
#include <string.h>

typedef struct ConfitGraphJsonBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitGraphJsonBuilder;

static char *confit_graph_copy_string(const char *text) {
  char *copy;
  size_t size;

  if (text == 0) {
    return 0;
  }

  size = strlen(text);
  copy = (char *)malloc(size + 1U);
  if (copy == 0) {
    return 0;
  }
  memcpy(copy, text, size + 1U);
  return copy;
}

static ConfitGraph *confit_graph_create(void) {
  return (ConfitGraph *)calloc(1U, sizeof(ConfitGraph));
}

static ConfitStatus confit_graph_add_node(ConfitGraph *graph,
                                          const ConfitOption *option) {
  ConfitGraphNode *new_nodes;
  char *copy;

  copy = confit_graph_copy_string(option->id);
  if (copy == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  new_nodes =
      (ConfitGraphNode *)realloc(graph->nodes,
                                 (graph->node_count + 1U) *
                                     sizeof(graph->nodes[0]));
  if (new_nodes == 0) {
    free(copy);
    return CONFIT_ERR_INTERNAL;
  }

  graph->nodes = new_nodes;
  graph->nodes[graph->node_count].id = copy;
  graph->nodes[graph->node_count].type = option->type;
  graph->nodes[graph->node_count].is_visible =
      option->prompt != 0 && option->prompt[0] != '\0';
  graph->node_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_graph_add_edge(ConfitGraph *graph,
                                          const char *from, const char *to,
                                          ConfitDependencyKind kind) {
  ConfitGraphEdge *new_edges;
  char *from_copy;
  char *to_copy;

  from_copy = confit_graph_copy_string(from);
  if (from_copy == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  to_copy = confit_graph_copy_string(to);
  if (to_copy == 0) {
    free(from_copy);
    return CONFIT_ERR_INTERNAL;
  }

  new_edges =
      (ConfitGraphEdge *)realloc(graph->edges,
                                 (graph->edge_count + 1U) *
                                     sizeof(graph->edges[0]));
  if (new_edges == 0) {
    free(from_copy);
    free(to_copy);
    return CONFIT_ERR_INTERNAL;
  }

  graph->edges = new_edges;
  graph->edges[graph->edge_count].from = from_copy;
  graph->edges[graph->edge_count].to = to_copy;
  graph->edges[graph->edge_count].kind = kind;
  graph->edge_count += 1U;
  return CONFIT_OK;
}

static const ConfitOption *confit_graph_find_option(
    const ConfitProject *project, const char *id) {
  size_t index;

  for (index = 0U; index < project->option_count; ++index) {
    if (project->options[index].id != 0 &&
        strcmp(project->options[index].id, id) == 0) {
      return &project->options[index];
    }
  }
  return 0;
}

static size_t confit_graph_find_node_index(const ConfitGraph *graph,
                                           const char *id) {
  size_t index;

  if (graph == 0 || id == 0) {
    return (size_t)-1;
  }

  for (index = 0U; index < graph->node_count; ++index) {
    if (strcmp(graph->nodes[index].id, id) == 0) {
      return index;
    }
  }
  return (size_t)-1;
}

static const ConfitGraphNode *confit_graph_find_node(const ConfitGraph *graph,
                                                     const char *id) {
  const size_t index = confit_graph_find_node_index(graph, id);
  return index == (size_t)-1 ? 0 : &graph->nodes[index];
}

static int confit_graph_edge_is_cycle_edge(const ConfitGraphEdge *edge) {
  return edge->kind != CONFIT_DEPENDENCY_CONFLICTS;
}

static int confit_graph_edge_is_hard_edge(const ConfitGraphEdge *edge) {
  return edge->kind == CONFIT_DEPENDENCY_REQUIRES ||
         edge->kind == CONFIT_DEPENDENCY_FORCES;
}

static int confit_graph_same_unordered_pair(const ConfitGraphEdge *left,
                                            const ConfitGraphEdge *right) {
  return (strcmp(left->from, right->from) == 0 &&
          strcmp(left->to, right->to) == 0) ||
         (strcmp(left->from, right->to) == 0 &&
          strcmp(left->to, right->from) == 0);
}

ConfitStatus confit_graph_build(const ConfitProject *project,
                                ConfitGraph **out_graph,
                                ConfitDiagnostic *diagnostic) {
  ConfitGraph *graph;
  ConfitStatus status;
  size_t option_index;

  if (out_graph == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing graph output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  *out_graph = 0;
  if (project == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing project");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  graph = confit_graph_create();
  if (graph == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          "failed to allocate graph");
    return CONFIT_ERR_INTERNAL;
  }
  graph->project_name = confit_graph_copy_string(project->name);
  if (project->name != 0 && graph->project_name == 0) {
    confit_graph_free(graph);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          "failed to allocate graph project name");
    return CONFIT_ERR_INTERNAL;
  }

  for (option_index = 0U; option_index < project->option_count;
       ++option_index) {
    status = confit_graph_add_node(graph, &project->options[option_index]);
    if (status != CONFIT_OK) {
      confit_graph_free(graph);
      confit_diagnostic_set(diagnostic, status, 0, 0, 0,
                            "failed to add graph node");
      return status;
    }
  }

  for (option_index = 0U; option_index < project->option_count;
       ++option_index) {
    const ConfitOption *option = &project->options[option_index];
    size_t dependency_index;

    for (dependency_index = 0U; dependency_index < option->dependency_count;
         ++dependency_index) {
      const ConfitDependencyRef *dependency =
          &option->dependencies[dependency_index];

      if (strcmp(option->id, dependency->option_id) == 0) {
        confit_graph_free(graph);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_DEPENDENCY, option->id, 0,
                              0, "dependency self-edge");
        return CONFIT_ERR_DEPENDENCY;
      }
      if (confit_graph_find_option(project, dependency->option_id) == 0) {
        confit_graph_free(graph);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_DEPENDENCY, option->id, 0,
                              0, "unknown dependency option");
        return CONFIT_ERR_DEPENDENCY;
      }

      status = confit_graph_add_edge(graph, option->id, dependency->option_id,
                                     dependency->kind);
      if (status != CONFIT_OK) {
        confit_graph_free(graph);
        confit_diagnostic_set(diagnostic, status, option->id, 0, 0,
                              "failed to add graph edge");
        return status;
      }
    }
  }

  *out_graph = graph;
  return CONFIT_OK;
}

static int confit_graph_dfs_has_cycle(const ConfitGraph *graph,
                                      size_t node_index, unsigned char *colors,
                                      const char **cycle_node) {
  size_t edge_index;

  colors[node_index] = 1U;
  for (edge_index = 0U; edge_index < graph->edge_count; ++edge_index) {
    const ConfitGraphEdge *edge = &graph->edges[edge_index];
    size_t to_index;

    if (!confit_graph_edge_is_cycle_edge(edge) ||
        strcmp(edge->from, graph->nodes[node_index].id) != 0) {
      continue;
    }

    to_index = confit_graph_find_node_index(graph, edge->to);
    if (to_index == (size_t)-1) {
      continue;
    }
    if (colors[to_index] == 1U) {
      *cycle_node = edge->to;
      return 1;
    }
    if (colors[to_index] == 0U &&
        confit_graph_dfs_has_cycle(graph, to_index, colors, cycle_node)) {
      return 1;
    }
  }
  colors[node_index] = 2U;
  return 0;
}

static ConfitStatus confit_graph_validate_cycles(
    const ConfitGraph *graph, ConfitDiagnostic *diagnostic) {
  unsigned char *colors;
  size_t index;

  colors = (unsigned char *)calloc(graph->node_count, sizeof(colors[0]));
  if (graph->node_count > 0U && colors == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          "failed to allocate graph cycle state");
    return CONFIT_ERR_INTERNAL;
  }

  for (index = 0U; index < graph->node_count; ++index) {
    const char *cycle_node;

    if (colors[index] != 0U) {
      continue;
    }
    cycle_node = graph->nodes[index].id;
    if (confit_graph_dfs_has_cycle(graph, index, colors, &cycle_node)) {
      free(colors);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_DEPENDENCY, cycle_node, 0,
                            0, "dependency cycle");
      return CONFIT_ERR_DEPENDENCY;
    }
  }

  free(colors);
  return CONFIT_OK;
}

static int confit_graph_is_internal_id(const char *id) {
  return id != 0 && strstr(id, ".internal.") != 0;
}

static int confit_graph_node_has_requires(const ConfitGraph *graph,
                                          const char *id) {
  size_t index;

  for (index = 0U; index < graph->edge_count; ++index) {
    if (graph->edges[index].kind == CONFIT_DEPENDENCY_REQUIRES &&
        strcmp(graph->edges[index].from, id) == 0) {
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_graph_validate_forces(
    const ConfitGraph *graph, ConfitDiagnostic *diagnostic) {
  size_t index;

  for (index = 0U; index < graph->edge_count; ++index) {
    const ConfitGraphEdge *edge = &graph->edges[index];
    const ConfitGraphNode *target;

    if (edge->kind != CONFIT_DEPENDENCY_FORCES) {
      continue;
    }

    target = confit_graph_find_node(graph, edge->to);
    if (target != 0 && target->is_visible) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_DEPENDENCY, edge->from, 0,
                            0, "forces visible option");
      return CONFIT_ERR_DEPENDENCY;
    }
    if (!confit_graph_is_internal_id(edge->to)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_DEPENDENCY, edge->from, 0,
                            0, "forces non-internal option");
      return CONFIT_ERR_DEPENDENCY;
    }
    if (confit_graph_node_has_requires(graph, edge->to)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_DEPENDENCY, edge->from, 0,
                            0, "forces option with requirements");
      return CONFIT_ERR_DEPENDENCY;
    }
  }

  return CONFIT_OK;
}

static ConfitStatus confit_graph_validate_conflicts(
    const ConfitGraph *graph, ConfitDiagnostic *diagnostic) {
  size_t conflict_index;

  for (conflict_index = 0U; conflict_index < graph->edge_count;
       ++conflict_index) {
    const ConfitGraphEdge *conflict = &graph->edges[conflict_index];
    size_t edge_index;

    if (conflict->kind != CONFIT_DEPENDENCY_CONFLICTS) {
      continue;
    }

    for (edge_index = 0U; edge_index < graph->edge_count; ++edge_index) {
      const ConfitGraphEdge *edge = &graph->edges[edge_index];

      if (edge_index == conflict_index || !confit_graph_edge_is_hard_edge(edge)) {
        continue;
      }
      if (confit_graph_same_unordered_pair(conflict, edge)) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_CONFLICT, conflict->from,
                              0, 0, "conflict contradiction");
        return CONFIT_ERR_CONFLICT;
      }
    }
  }

  return CONFIT_OK;
}

ConfitStatus confit_graph_validate(const ConfitGraph *graph,
                                   ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  if (graph == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing graph");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_graph_validate_cycles(graph, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_graph_validate_forces(graph, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_graph_validate_conflicts(graph, diagnostic);
}

void confit_graph_free(ConfitGraph *graph) {
  size_t index;

  if (graph == 0) {
    return;
  }

  free(graph->project_name);

  for (index = 0U; index < graph->node_count; ++index) {
    free(graph->nodes[index].id);
  }
  free(graph->nodes);

  for (index = 0U; index < graph->edge_count; ++index) {
    free(graph->edges[index].from);
    free(graph->edges[index].to);
  }
  free(graph->edges);
  free(graph);
}

static void confit_graph_json_builder_init(ConfitGraphJsonBuilder *builder) {
  builder->text = 0;
  builder->size = 0U;
  builder->capacity = 0U;
}

static ConfitStatus confit_graph_json_builder_reserve(
    ConfitGraphJsonBuilder *builder, size_t additional_size) {
  size_t required_capacity;
  size_t new_capacity;
  char *new_text;

  required_capacity = builder->size + additional_size + 1U;
  if (required_capacity <= builder->capacity) {
    return CONFIT_OK;
  }

  new_capacity = builder->capacity == 0U ? 128U : builder->capacity;
  while (new_capacity < required_capacity) {
    new_capacity *= 2U;
  }

  new_text = (char *)realloc(builder->text, new_capacity);
  if (new_text == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  builder->text = new_text;
  builder->capacity = new_capacity;
  return CONFIT_OK;
}

static ConfitStatus confit_graph_json_append(ConfitGraphJsonBuilder *builder,
                                             const char *text) {
  const size_t size = strlen(text);
  ConfitStatus status;

  status = confit_graph_json_builder_reserve(builder, size);
  if (status != CONFIT_OK) {
    return status;
  }

  memcpy(builder->text + builder->size, text, size);
  builder->size += size;
  builder->text[builder->size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_graph_json_append_escaped(
    ConfitGraphJsonBuilder *builder, const char *text) {
  size_t index;
  ConfitStatus status;

  status = confit_graph_json_append(builder, "\"");
  if (status != CONFIT_OK) {
    return status;
  }

  for (index = 0U; text[index] != '\0'; ++index) {
    char ch[2];

    if (text[index] == '"' || text[index] == '\\') {
      status = confit_graph_json_append(builder, "\\");
      if (status != CONFIT_OK) {
        return status;
      }
    }
    ch[0] = text[index];
    ch[1] = '\0';
    status = confit_graph_json_append(builder, ch);
    if (status != CONFIT_OK) {
      return status;
    }
  }

  return confit_graph_json_append(builder, "\"");
}

ConfitStatus confit_graph_to_json(const ConfitGraph *graph, char **out_json) {
  ConfitGraphJsonBuilder builder;
  ConfitStatus status;
  size_t index;

  if (out_json == 0 || graph == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  *out_json = 0;
  confit_graph_json_builder_init(&builder);

#define CONFIT_JSON_APPEND(fragment)                                            \
  do {                                                                          \
    status = confit_graph_json_append(&builder, (fragment));                    \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_JSON_APPEND_ESCAPED(fragment)                                    \
  do {                                                                          \
    status = confit_graph_json_append_escaped(&builder, (fragment));            \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_JSON_APPEND("{\n");
  CONFIT_JSON_APPEND("  \"schema\": \"confit-graph-v1\",\n");
  CONFIT_JSON_APPEND("  \"project\": ");
  CONFIT_JSON_APPEND_ESCAPED(graph->project_name != 0 ? graph->project_name : "");
  CONFIT_JSON_APPEND(",\n");
  CONFIT_JSON_APPEND("  \"nodes\": [\n");
  for (index = 0U; index < graph->node_count; ++index) {
    CONFIT_JSON_APPEND("    {\"id\": ");
    CONFIT_JSON_APPEND_ESCAPED(graph->nodes[index].id);
    CONFIT_JSON_APPEND(", \"type\": ");
    CONFIT_JSON_APPEND_ESCAPED(confit_option_type_name(graph->nodes[index].type));
    CONFIT_JSON_APPEND("}");
    CONFIT_JSON_APPEND(index + 1U < graph->node_count ? ",\n" : "\n");
  }
  CONFIT_JSON_APPEND("  ],\n");
  CONFIT_JSON_APPEND("  \"edges\": [\n");
  for (index = 0U; index < graph->edge_count; ++index) {
    CONFIT_JSON_APPEND("    {\"from\": ");
    CONFIT_JSON_APPEND_ESCAPED(graph->edges[index].from);
    CONFIT_JSON_APPEND(", \"to\": ");
    CONFIT_JSON_APPEND_ESCAPED(graph->edges[index].to);
    CONFIT_JSON_APPEND(", \"kind\": ");
    CONFIT_JSON_APPEND_ESCAPED(
        confit_dependency_kind_name(graph->edges[index].kind));
    CONFIT_JSON_APPEND("}");
    CONFIT_JSON_APPEND(index + 1U < graph->edge_count ? ",\n" : "\n");
  }
  CONFIT_JSON_APPEND("  ]\n");
  CONFIT_JSON_APPEND("}\n");

#undef CONFIT_JSON_APPEND
#undef CONFIT_JSON_APPEND_ESCAPED

  *out_json = builder.text;
  return CONFIT_OK;
}

void confit_graph_string_free(char *text) { free(text); }
