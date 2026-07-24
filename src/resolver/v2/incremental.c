#include "snapshot_internal.h"

#include <stdlib.h>
#include <string.h>

static const char kInvalidIncrementalArgument[] =
    "invalid schema v2 incremental resolve argument";
static const char kUnknownChangedOption[] =
    "schema v2 incremental edit references unknown option";
static const char kAllocationFailed[] =
    "failed to allocate schema v2 incremental invalidation set";

static int confit_v2_invalidation_node_compare(const void *left,
                                                const void *right) {
  const ConfitV2InvalidationNode *left_node =
      (const ConfitV2InvalidationNode *)left;
  const ConfitV2InvalidationNode *right_node =
      (const ConfitV2InvalidationNode *)right;

  if (left_node->kind != right_node->kind) {
    return left_node->kind < right_node->kind ? -1 : 1;
  }
  return strcmp(left_node->id, right_node->id);
}

static int confit_v2_reverse_node_compare(const void *left, const void *right) {
  const ConfitV2ReverseNode *left_node = (const ConfitV2ReverseNode *)left;
  const ConfitV2ReverseNode *right_node = (const ConfitV2ReverseNode *)right;

  return confit_v2_invalidation_node_compare(&left_node->node,
                                              &right_node->node);
}

static ConfitStatus confit_v2_snapshot_reverse_append(
    ConfitV2Snapshot *snapshot, ConfitV2InvalidationKind kind, const char *id,
    size_t *out_index) {
  ConfitV2ReverseNode *grown;
  ConfitV2ReverseNode *node;

  if (snapshot->reverse_node_count == SIZE_MAX / sizeof(*snapshot->reverse_nodes)) {
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2ReverseNode *)realloc(
      snapshot->reverse_nodes,
      (snapshot->reverse_node_count + 1U) * sizeof(*grown));
  if (grown == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  snapshot->reverse_nodes = grown;
  node = &snapshot->reverse_nodes[snapshot->reverse_node_count];
  memset(node, 0, sizeof(*node));
  node->owned_id = confit_v2_ledger_strdup(id);
  if (node->owned_id == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  node->node.kind = kind;
  node->node.id = node->owned_id;
  if (out_index != 0) {
    *out_index = snapshot->reverse_node_count;
  }
  snapshot->reverse_node_count += 1U;
  return CONFIT_OK;
}

static size_t confit_v2_snapshot_reverse_lower_bound(
    const ConfitV2Snapshot *snapshot, ConfitV2InvalidationKind kind,
    const char *id) {
  ConfitV2InvalidationNode wanted;
  size_t low = 0U;
  size_t high = snapshot->reverse_node_count;

  wanted.kind = kind;
  wanted.id = id;
  while (low < high) {
    const size_t middle = low + (high - low) / 2U;
    if (confit_v2_invalidation_node_compare(&snapshot->reverse_nodes[middle].node,
                                            &wanted) < 0) {
      low = middle + 1U;
    } else {
      high = middle;
    }
  }
  return low;
}

const ConfitV2ReverseNode *confit_v2_snapshot_reverse_node_find(
    const ConfitV2Snapshot *snapshot, ConfitV2InvalidationKind kind,
    const char *id, size_t *out_index) {
  const size_t index = confit_v2_snapshot_reverse_lower_bound(snapshot, kind, id);

  if (index >= snapshot->reverse_node_count ||
      snapshot->reverse_nodes[index].node.kind != kind ||
      strcmp(snapshot->reverse_nodes[index].node.id, id) != 0) {
    return 0;
  }
  if (out_index != 0) {
    *out_index = index;
  }
  return &snapshot->reverse_nodes[index];
}

static ConfitV2InvalidationKind confit_v2_snapshot_owner_kind(
    const ConfitV2Snapshot *snapshot, ConfitV2CompiledGraphKind graph_kind,
    const char *owner_id) {
  if (graph_kind == CONFIT_V2_COMPILED_GRAPH_CONSTRAINT) {
    return CONFIT_V2_INVALIDATION_CONSTRAINT;
  }
  if (confit_v2_snapshot_reverse_node_find(snapshot,
                                            CONFIT_V2_INVALIDATION_OPTION,
                                            owner_id, 0) != 0) {
    return CONFIT_V2_INVALIDATION_OPTION;
  }
  return CONFIT_V2_INVALIDATION_CHOICE;
}

static ConfitStatus confit_v2_snapshot_reverse_add_dependency(
    ConfitV2Snapshot *snapshot, size_t from_index, size_t dependent_index) {
  ConfitV2ReverseNode *from = &snapshot->reverse_nodes[from_index];
  size_t *grown;
  size_t index;

  for (index = 0U; index < from->dependent_count; ++index) {
    if (from->dependents[index] == dependent_index) {
      return CONFIT_OK;
    }
  }
  if (from->dependent_count == SIZE_MAX / sizeof(*from->dependents)) {
    return CONFIT_ERR_INTERNAL;
  }
  grown = (size_t *)realloc(from->dependents,
                            (from->dependent_count + 1U) * sizeof(*grown));
  if (grown == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  from->dependents = grown;
  from->dependents[from->dependent_count] = dependent_index;
  from->dependent_count += 1U;
  return CONFIT_OK;
}

ConfitStatus confit_v2_snapshot_build_reverse_index(
    ConfitV2Snapshot *snapshot, const ConfitV2CompiledStructure *compiled,
    ConfitDiagnostic *diagnostic) {
  static const ConfitV2CompiledGraphKind kGraphKinds[] = {
      CONFIT_V2_COMPILED_GRAPH_EVALUATION,
      CONFIT_V2_COMPILED_GRAPH_VISIBILITY,
      CONFIT_V2_COMPILED_GRAPH_CHOICE,
      CONFIT_V2_COMPILED_GRAPH_CONSTRAINT,
  };
  size_t index;
  ConfitStatus status = CONFIT_OK;

  for (index = 0U; index < snapshot->option_count && status == CONFIT_OK;
       ++index) {
    status = confit_v2_snapshot_reverse_append(
        snapshot, CONFIT_V2_INVALIDATION_OPTION, snapshot->options[index].id, 0);
  }
  for (index = 0U; index < snapshot->choice_count && status == CONFIT_OK;
       ++index) {
    status = confit_v2_snapshot_reverse_append(
        snapshot, CONFIT_V2_INVALIDATION_CHOICE, snapshot->choices[index].id, 0);
  }
  for (index = 0U; index < snapshot->constraint_count && status == CONFIT_OK;
       ++index) {
    status = confit_v2_snapshot_reverse_append(
        snapshot, CONFIT_V2_INVALIDATION_CONSTRAINT,
        snapshot->constraints[index].id, 0);
  }
  if (status != CONFIT_OK) {
    goto fail;
  }
  qsort(snapshot->reverse_nodes, snapshot->reverse_node_count,
        sizeof(*snapshot->reverse_nodes), confit_v2_reverse_node_compare);
  for (index = 0U; index < sizeof(kGraphKinds) / sizeof(kGraphKinds[0]) &&
                   status == CONFIT_OK;
       ++index) {
    const ConfitV2CompiledGraph *graph =
        confit_v2_compiled_structure_graph(compiled, kGraphKinds[index]);
    size_t edge_index;

    for (edge_index = 0U; edge_index < graph->edge_count; ++edge_index) {
      const ConfitV2CompiledGraphEdge *edge = &graph->edges[edge_index];
      const ConfitV2InvalidationKind owner_kind =
          confit_v2_snapshot_owner_kind(snapshot, kGraphKinds[index],
                                        edge->owner_id);
      size_t source_index;
      size_t owner_index;

      if (confit_v2_snapshot_reverse_node_find(
              snapshot, CONFIT_V2_INVALIDATION_OPTION, edge->target->id,
              &source_index) == 0 ||
          confit_v2_snapshot_reverse_node_find(snapshot, owner_kind,
                                                edge->owner_id,
                                                &owner_index) == 0) {
        continue;
      }
      status = confit_v2_snapshot_reverse_add_dependency(snapshot, source_index,
                                                          owner_index);
      if (status != CONFIT_OK) {
        break;
      }
    }
  }
  if (status == CONFIT_OK) {
    return CONFIT_OK;
  }

fail:
  confit_v2_ledger_diagnostic(0, 0U, 0U, status, kAllocationFailed, diagnostic);
  return status;
}

static ConfitStatus confit_v2_invalidation_set_append(
    ConfitV2InvalidationSet *set, const ConfitV2InvalidationNode *node) {
  ConfitV2InvalidationNode *grown;
  char *id;

  if (set->count == SIZE_MAX / sizeof(*set->nodes)) {
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2InvalidationNode *)realloc(
      set->nodes, (set->count + 1U) * sizeof(*grown));
  if (grown == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  set->nodes = grown;
  id = confit_v2_ledger_strdup(node->id);
  if (id == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  set->nodes[set->count].kind = node->kind;
  set->nodes[set->count].id = id;
  set->count += 1U;
  return CONFIT_OK;
}

ConfitStatus confit_v2_snapshot_invalidate(
    const ConfitV2Snapshot *snapshot, const char *changed_option_id,
    ConfitV2InvalidationSet **out_set, ConfitDiagnostic *diagnostic) {
  size_t *queue;
  unsigned char *visited;
  ConfitV2InvalidationSet *set;
  size_t start;
  size_t head = 0U;
  size_t tail = 0U;
  ConfitStatus status = CONFIT_OK;

  if (snapshot == 0 || changed_option_id == 0 || out_set == 0) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INVALID_ARGUMENT,
                                kInvalidIncrementalArgument, diagnostic);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_set = 0;
  if (confit_v2_snapshot_reverse_node_find(snapshot,
                                            CONFIT_V2_INVALIDATION_OPTION,
                                            changed_option_id, &start) == 0) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INVALID_ARGUMENT,
                                kUnknownChangedOption, diagnostic);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  set = (ConfitV2InvalidationSet *)calloc(1U, sizeof(*set));
  queue = (size_t *)calloc(snapshot->reverse_node_count, sizeof(*queue));
  visited = (unsigned char *)calloc(snapshot->reverse_node_count, sizeof(*visited));
  if (set == 0 || queue == 0 || visited == 0) {
    free(queue);
    free(visited);
    free(set);
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INTERNAL,
                                kAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  queue[tail++] = start;
  visited[start] = 1U;
  while (head < tail && status == CONFIT_OK) {
    const size_t current = queue[head++];
    const ConfitV2ReverseNode *node = &snapshot->reverse_nodes[current];
    size_t index;

    status = confit_v2_invalidation_set_append(set, &node->node);
    for (index = 0U; index < node->dependent_count && status == CONFIT_OK;
         ++index) {
      const size_t dependent = node->dependents[index];
      if (!visited[dependent]) {
        visited[dependent] = 1U;
        queue[tail++] = dependent;
      }
    }
  }
  free(queue);
  free(visited);
  if (status != CONFIT_OK) {
    confit_v2_invalidation_set_free(set);
    confit_v2_ledger_diagnostic(0, 0U, 0U, status, kAllocationFailed,
                                diagnostic);
    return status;
  }
  qsort(set->nodes, set->count, sizeof(*set->nodes),
        confit_v2_invalidation_node_compare);
  *out_set = set;
  return CONFIT_OK;
}

void confit_v2_invalidation_set_free(ConfitV2InvalidationSet *set) {
  size_t index;

  if (set == 0) {
    return;
  }
  for (index = 0U; index < set->count; ++index) {
    free((char *)set->nodes[index].id);
  }
  free(set->nodes);
  free(set);
}

size_t confit_v2_invalidation_set_count(const ConfitV2InvalidationSet *set) {
  return set != 0 ? set->count : 0U;
}

const ConfitV2InvalidationNode *confit_v2_invalidation_set_at(
    const ConfitV2InvalidationSet *set, size_t index) {
  return set != 0 && index < set->count ? &set->nodes[index] : 0;
}

ConfitStatus confit_v2_snapshot_reconcile_edit(
    const ConfitV2Snapshot *base,
    const ConfitV2CompiledStructure *compiled,
    const ConfitV2LedgerOptions *options, const char *changed_option_id,
    ConfitV2Snapshot **out_snapshot, ConfitV2InvalidationSet **out_affected,
    ConfitDiagnostic *diagnostic) {
  ConfitV2InvalidationSet *affected = 0;
  ConfitStatus status;

  if (base == 0 || compiled == 0 || changed_option_id == 0 ||
      out_snapshot == 0) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INVALID_ARGUMENT,
                                kInvalidIncrementalArgument, diagnostic);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_snapshot = 0;
  if (out_affected != 0) {
    *out_affected = 0;
  }
  status = confit_v2_snapshot_invalidate(base, changed_option_id, &affected,
                                          diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_snapshot_resolve(compiled, options, out_snapshot,
                                        diagnostic);
  }
  if (status != CONFIT_OK) {
    confit_v2_invalidation_set_free(affected);
    return status;
  }
  if (out_affected != 0) {
    *out_affected = affected;
  } else {
    confit_v2_invalidation_set_free(affected);
  }
  return CONFIT_OK;
}
