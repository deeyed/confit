#include "compiled_internal.h"

#include <stdlib.h>

static const char kAllocationFailed[] =
    "failed to allocate schema v2 structure graph";

static size_t confit_v2_graph_slot(ConfitV2CompiledGraphKind kind) {
  switch (kind) {
  case CONFIT_V2_COMPILED_GRAPH_EVALUATION:
    return 0U;
  case CONFIT_V2_COMPILED_GRAPH_VISIBILITY:
    return 1U;
  case CONFIT_V2_COMPILED_GRAPH_CHOICE:
    return 2U;
  case CONFIT_V2_COMPILED_GRAPH_CONSTRAINT:
    return 3U;
  default:
    return 4U;
  }
}

static ConfitStatus confit_v2_graph_append(
    ConfitV2CompiledGraph *graph, const char *owner_id,
    const ConfitV2Symbol *target, const ConfitV2SourceSpan *span,
    ConfitDiagnostic *diagnostic) {
  ConfitV2CompiledGraphEdge *grown;

  if (graph->edge_count == graph->edge_capacity) {
    size_t capacity = graph->edge_capacity == 0U ? 16U : graph->edge_capacity * 2U;

    if (capacity < graph->edge_capacity ||
        capacity > SIZE_MAX / sizeof(*graph->edges)) {
      confit_v2_structure_diagnostic(span, CONFIT_ERR_INTERNAL, kAllocationFailed,
                                     diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
    grown = (ConfitV2CompiledGraphEdge *)realloc(
        graph->edges, capacity * sizeof(*graph->edges));
    if (grown == 0) {
      confit_v2_structure_diagnostic(span, CONFIT_ERR_INTERNAL, kAllocationFailed,
                                     diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
    graph->edges = grown;
    graph->edge_capacity = capacity;
  }
  graph->edges[graph->edge_count].owner_id = owner_id;
  graph->edges[graph->edge_count].target = target;
  graph->edges[graph->edge_count].span = span;
  graph->edge_count += 1U;
  return CONFIT_OK;
}

static ConfitV2CompiledGraphKind confit_v2_expression_graph_kind(
    ConfitV2LinkedExpressionRole role) {
  switch (role) {
  case CONFIT_V2_LINKED_EXPRESSION_COMPUTED:
  case CONFIT_V2_LINKED_EXPRESSION_AVAILABLE_IF:
  case CONFIT_V2_LINKED_EXPRESSION_DEFAULT_WHEN:
  case CONFIT_V2_LINKED_EXPRESSION_SUGGESTION_WHEN:
  case CONFIT_V2_LINKED_EXPRESSION_CHOICE_AVAILABLE_IF:
    return CONFIT_V2_COMPILED_GRAPH_EVALUATION;
  case CONFIT_V2_LINKED_EXPRESSION_VISIBLE_IF:
  case CONFIT_V2_LINKED_EXPRESSION_MENU_VISIBLE_IF:
  case CONFIT_V2_LINKED_EXPRESSION_CHOICE_VISIBLE_IF:
    return CONFIT_V2_COMPILED_GRAPH_VISIBILITY;
  case CONFIT_V2_LINKED_EXPRESSION_CHOICE_DEFAULT_WHEN:
    return CONFIT_V2_COMPILED_GRAPH_CHOICE;
  case CONFIT_V2_LINKED_EXPRESSION_CONSTRAINT_WHEN:
  case CONFIT_V2_LINKED_EXPRESSION_CONSTRAINT_REQUIRE:
    return CONFIT_V2_COMPILED_GRAPH_CONSTRAINT;
  default:
    return 0;
  }
}

static ConfitStatus confit_v2_graph_append_expression(
    ConfitV2CompiledStructure *compiled,
    const ConfitV2LinkedExpression *expression, ConfitDiagnostic *diagnostic) {
  const ConfitV2CompiledGraphKind kind =
      confit_v2_expression_graph_kind(expression->role);
  ConfitV2CompiledGraph *graph;
  size_t index;

  if (kind == 0) {
    return CONFIT_OK;
  }
  graph = &compiled->graphs[confit_v2_graph_slot(kind)];
  for (index = 0U; index < expression->reference_count; ++index) {
    ConfitStatus status = confit_v2_graph_append(
        graph, expression->owner_id, expression->references[index].symbol,
        &expression->expression->source_span, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

ConfitStatus confit_v2_structure_build_graphs(ConfitV2CompiledStructure *compiled,
                                               ConfitDiagnostic *diagnostic) {
  static const ConfitV2CompiledGraphKind kKinds[] = {
      CONFIT_V2_COMPILED_GRAPH_EVALUATION,
      CONFIT_V2_COMPILED_GRAPH_VISIBILITY,
      CONFIT_V2_COMPILED_GRAPH_CHOICE,
      CONFIT_V2_COMPILED_GRAPH_CONSTRAINT,
  };
  size_t index;

  for (index = 0U; index < sizeof(kKinds) / sizeof(kKinds[0]); ++index) {
    compiled->graphs[index].kind = kKinds[index];
  }
  for (index = 0U;
       index < confit_v2_linked_project_expression_count(compiled->linked);
       ++index) {
    ConfitStatus status = confit_v2_graph_append_expression(
        compiled, confit_v2_linked_project_expression_at(compiled->linked, index),
        diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  for (index = 0U; index < compiled->choice_count; ++index) {
    const ConfitV2CompiledChoice *choice = &compiled->choices[index];
    ConfitV2CompiledGraph *graph =
        &compiled->graphs[confit_v2_graph_slot(CONFIT_V2_COMPILED_GRAPH_CHOICE)];
    size_t member_index;
    for (member_index = 0U; member_index < choice->member_count; ++member_index) {
      ConfitStatus status = confit_v2_graph_append(
          graph, choice->source->id, choice->members[member_index],
          &choice->source->span, diagnostic);
      if (status != CONFIT_OK) {
        return status;
      }
    }
  }
  return CONFIT_OK;
}

void confit_v2_structure_graphs_clear(ConfitV2CompiledStructure *compiled) {
  size_t index;

  if (compiled == 0) {
    return;
  }
  for (index = 0U; index < 4U; ++index) {
    free(compiled->graphs[index].edges);
    compiled->graphs[index].edges = 0;
    compiled->graphs[index].edge_count = 0U;
    compiled->graphs[index].edge_capacity = 0U;
  }
}

const ConfitV2CompiledGraph *confit_v2_compiled_structure_graph(
    const ConfitV2CompiledStructure *compiled, ConfitV2CompiledGraphKind kind) {
  const size_t slot = confit_v2_graph_slot(kind);

  if (compiled == 0 || slot >= 4U) {
    return 0;
  }
  return &compiled->graphs[slot];
}
