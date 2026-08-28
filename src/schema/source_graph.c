#include "confit/source.h"

#include <stdint.h>
#include <string.h>

#include "confit/limits.h"
#include "source_internal.h"

typedef enum ConfitSourceNodeState {
  CONFIT_SOURCE_NODE_VISITING = 1,
  CONFIT_SOURCE_NODE_VISITED = 2,
} ConfitSourceNodeState;

typedef struct ConfitSourceNode {
  ConfitInputImage *input;
  size_t parent;
  size_t source_ordinal;
  size_t include_depth;
  ConfitSourceNodeState state;
} ConfitSourceNode;

struct ConfitSourceGraph {
  ConfitAllocator allocator;
  ConfitSourceNode *nodes;
  size_t node_count;
  size_t node_capacity;
  size_t edge_count;
  size_t total_bytes;
};

static const char kInvalidArgument[] = "invalid source-graph argument";
static const char kInvalidAllocator[] = "allocator capability is incomplete";
static const char kInvalidSourcePath[] =
    "source path is not a normalized relative TOML path";
static const char kMissingEntrySource[] =
    "entry document requires a top-level source array";
static const char kInvalidSourceArray[] =
    "source membership must be an array of strings";
static const char kInvalidMenuContainer[] =
    "fragment menu source carrier must be a table";
static const char kDuplicateInclude[] =
    "source fragment is included more than once";
static const char kIncludeCycle[] = "source graph contains an include cycle";
static const char kFileIdentityCollision[] =
    "distinct source paths resolve to the same regular file";
static const char kFragmentLimit[] = "source fragment limit is exceeded";
static const char kEdgeLimit[] = "source edge limit is exceeded";
static const char kDepthLimit[] = "source include depth limit is exceeded";
static const char kLedgerLimit[] = "source read ledger capacity is exceeded";
static const char kOutOfMemory[] = "failed to allocate a source graph";

static ConfitStatus confit_source_fail(ConfitDiagnostic *diagnostic,
                                       ConfitStatus status,
                                       const char *path,
                                       const char *message) {
  confit_diagnostic_set(diagnostic, status, path, 0U, 0U, message);
  return status;
}

static int confit_source_resolve_allocator(const ConfitAllocator *requested,
                                           ConfitAllocator *resolved) {
  if (resolved == 0) {
    return 0;
  }
  if (requested == 0) {
    confit_allocator_default(resolved);
    return 1;
  }
  if (!confit_allocator_is_valid(requested)) {
    return 0;
  }
  *resolved = *requested;
  return 1;
}

static int confit_source_has_toml_suffix(const char *path) {
  const size_t size = strlen(path);
  return size >= 5U && memcmp(path + size - 5U, ".toml", 5U) == 0;
}

static int confit_source_path_is_valid(const char *path) {
  return path != 0 && confit_host_relative_path_is_valid(path) &&
         confit_source_has_toml_suffix(path);
}

static int confit_source_copy_literal(const ConfitTomlValue *value,
                                      char *out_path) {
  const char *text = 0;
  size_t index;
  size_t size = 0U;
  if (!confit_toml_value_string(value, &text, &size) || text == 0 ||
      size == 0U || size > CONFIT_LIMIT_SOURCE_PATH_BYTES) {
    return 0;
  }
  for (index = 0U; index < size; ++index) {
    if (text[index] == '\0') {
      return 0;
    }
  }
  memcpy(out_path, text, size);
  out_path[size] = '\0';
  return confit_source_path_is_valid(out_path);
}

static int confit_source_identity_equal(const ConfitHostFileIdentity *left,
                                        const ConfitHostFileIdentity *right) {
  return left->device == right->device && left->inode == right->inode;
}

static ConfitStatus confit_source_grow_nodes(ConfitSourceGraph *graph,
                                             ConfitDiagnostic *diagnostic) {
  ConfitSourceNode *replacement;
  size_t capacity;
  size_t bytes;
  if (graph->node_count < graph->node_capacity) {
    return CONFIT_OK;
  }
  if (graph->node_count >= CONFIT_LIMIT_SOURCE_FRAGMENTS) {
    return confit_source_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                              kFragmentLimit);
  }
  capacity = graph->node_capacity == 0U ? 8U : graph->node_capacity * 2U;
  if (capacity > CONFIT_LIMIT_SOURCE_FRAGMENTS) {
    capacity = CONFIT_LIMIT_SOURCE_FRAGMENTS;
  }
  if (capacity < graph->node_count ||
      capacity > SIZE_MAX / sizeof(*replacement)) {
    return confit_source_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                              kOutOfMemory);
  }
  bytes = capacity * sizeof(*replacement);
  replacement =
      (ConfitSourceNode *)graph->allocator.allocate(graph->allocator.context,
                                                    bytes);
  if (replacement == 0) {
    return confit_source_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                              kOutOfMemory);
  }
  if (graph->node_count > 0U) {
    memcpy(replacement, graph->nodes,
           graph->node_count * sizeof(*replacement));
  }
  if (graph->nodes != 0) {
    graph->allocator.deallocate(graph->allocator.context, graph->nodes);
  }
  graph->nodes = replacement;
  graph->node_capacity = capacity;
  return CONFIT_OK;
}

void confit_source_read_ledger_init(ConfitSourceReadLedger *ledger,
                                    ConfitSourceReadRecord *records,
                                    size_t capacity) {
  if (ledger == 0) {
    return;
  }
  ledger->records = records;
  ledger->capacity = records != 0 ? capacity : 0U;
  ledger->count = 0U;
}

static ConfitStatus confit_source_record_read(
    ConfitSourceReadLedger *ledger, const ConfitInputImage *image,
    ConfitSourceReadPurpose purpose, ConfitDiagnostic *diagnostic) {
  ConfitSourceReadRecord *record;
  const char *path;
  size_t byte_count = 0U;
  size_t path_size;
  if (ledger == 0) {
    return CONFIT_OK;
  }
  if (ledger->count >= ledger->capacity) {
    return confit_source_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                              kLedgerLimit);
  }
  path = confit_input_image_path(image);
  path_size = strlen(path);
  if (path_size > CONFIT_LIMIT_SOURCE_PATH_BYTES ||
      confit_input_image_bytes(image, &byte_count) == 0) {
    return confit_source_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                              kInvalidArgument);
  }
  record = &ledger->records[ledger->count];
  memcpy(record->path, path, path_size + 1U);
  record->purpose = purpose;
  record->byte_count = byte_count;
  ledger->count += 1U;
  return CONFIT_OK;
}

static int confit_source_find_index(const ConfitSourceGraph *graph,
                                    const char *path, size_t *out_index) {
  size_t index;
  for (index = 0U; index < graph->node_count; ++index) {
    if (strcmp(confit_input_image_path(graph->nodes[index].input), path) == 0) {
      if (out_index != 0) {
        *out_index = index;
      }
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_source_extract_array(
    const ConfitInputImage *image, int is_entry,
    const ConfitTomlValue **out_array, ConfitDiagnostic *diagnostic) {
  const ConfitTomlValue *root;
  const ConfitTomlValue *carrier;
  const ConfitTomlValue *source;
  const char *path = confit_input_image_path(image);
  root = confit_toml_document_root(confit_input_image_document(image));
  if (confit_toml_value_type(root) != CONFIT_TOML_VALUE_TABLE) {
    return confit_source_fail(diagnostic, CONFIT_ERR_VALIDATION, path,
                              kInvalidSourceArray);
  }
  if (is_entry) {
    source = confit_toml_table_find(root, "source");
    if (source == 0) {
      return confit_source_fail(diagnostic, CONFIT_ERR_VALIDATION, path,
                                kMissingEntrySource);
    }
  } else {
    carrier = confit_toml_table_find(root, "menu");
    if (carrier == 0) {
      *out_array = 0;
      return CONFIT_OK;
    }
    if (confit_toml_value_type(carrier) != CONFIT_TOML_VALUE_TABLE) {
      return confit_source_fail(diagnostic, CONFIT_ERR_VALIDATION, path,
                                kInvalidMenuContainer);
    }
    source = confit_toml_table_find(carrier, "source");
    if (source == 0) {
      *out_array = 0;
      return CONFIT_OK;
    }
  }
  if (confit_toml_value_type(source) != CONFIT_TOML_VALUE_ARRAY) {
    return confit_source_fail(diagnostic, CONFIT_ERR_VALIDATION, path,
                              kInvalidSourceArray);
  }
  *out_array = source;
  return CONFIT_OK;
}

static ConfitStatus confit_source_load_node(
    ConfitSourceGraph *graph, ConfitHostRoot *project_root,
    const char *path, size_t parent, size_t source_ordinal,
    size_t include_depth, int is_entry, ConfitSourceReadLedger *ledger,
    ConfitDiagnostic *diagnostic);

ConfitStatus confit_source_budget_preflight(
    size_t current_nodes, size_t current_edges, size_t source_count,
    ConfitDiagnostic *diagnostic) {
  if (current_edges > CONFIT_LIMIT_SOURCE_EDGES ||
      source_count > CONFIT_LIMIT_SOURCE_EDGES - current_edges) {
    return confit_source_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                              kEdgeLimit);
  }
  if (current_nodes > CONFIT_LIMIT_SOURCE_FRAGMENTS ||
      source_count > CONFIT_LIMIT_SOURCE_FRAGMENTS - current_nodes) {
    return confit_source_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                              kFragmentLimit);
  }
  return CONFIT_OK;
}

static ConfitStatus confit_source_load_children(
    ConfitSourceGraph *graph, ConfitHostRoot *project_root, size_t node_index,
    int is_entry, ConfitSourceReadLedger *ledger,
    ConfitDiagnostic *diagnostic) {
  const ConfitTomlValue *sources = 0;
  size_t source_count;
  size_t ordinal;
  ConfitStatus status;

  status = confit_source_extract_array(graph->nodes[node_index].input, is_entry,
                                       &sources, diagnostic);
  if (status != CONFIT_OK || sources == 0) {
    return status;
  }
  source_count = confit_toml_array_size(sources);
  status = confit_source_budget_preflight(graph->node_count,
                                          graph->edge_count, source_count,
                                          diagnostic);
  if (status != CONFIT_OK) {
    if (diagnostic != 0) {
      diagnostic->path =
          confit_input_image_path(graph->nodes[node_index].input);
    }
    return status;
  }
  graph->edge_count += source_count;
  for (ordinal = 0U; ordinal < source_count; ++ordinal) {
    char child_path[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
    const ConfitTomlValue *value = confit_toml_array_at(sources, ordinal);
    const size_t child_depth = graph->nodes[node_index].include_depth + 1U;
    if (!confit_source_copy_literal(value, child_path)) {
      return confit_source_fail(diagnostic, CONFIT_ERR_VALIDATION,
                                confit_input_image_path(
                                    graph->nodes[node_index].input),
                                kInvalidSourcePath);
    }
    if (child_depth > CONFIT_LIMIT_INCLUDE_DEPTH) {
      return confit_source_fail(diagnostic, CONFIT_ERR_VALIDATION,
                                confit_input_image_path(
                                    graph->nodes[node_index].input),
                                kDepthLimit);
    }
    status = confit_source_load_node(
        graph, project_root, child_path, node_index, ordinal, child_depth, 0,
        ledger, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_source_load_node(
    ConfitSourceGraph *graph, ConfitHostRoot *project_root,
    const char *path, size_t parent, size_t source_ordinal,
    size_t include_depth, int is_entry, ConfitSourceReadLedger *ledger,
    ConfitDiagnostic *diagnostic) {
  ConfitHostFileIdentity candidate_identity;
  ConfitHostFileIdentity existing_identity;
  ConfitInputImage *image = 0;
  size_t byte_count = 0U;
  size_t candidate_total = graph->total_bytes;
  size_t existing_index;
  size_t index;
  ConfitStatus status;

  if (!confit_source_path_is_valid(path)) {
    return confit_source_fail(diagnostic, CONFIT_ERR_VALIDATION, path,
                              kInvalidSourcePath);
  }
  if (confit_source_find_index(graph, path, &existing_index)) {
    return confit_source_fail(
        diagnostic, CONFIT_ERR_VALIDATION, path,
        graph->nodes[existing_index].state == CONFIT_SOURCE_NODE_VISITING
            ? kIncludeCycle
            : kDuplicateInclude);
  }
  if (graph->node_count >= CONFIT_LIMIT_SOURCE_FRAGMENTS) {
    return confit_source_fail(diagnostic, CONFIT_ERR_VALIDATION, path,
                              kFragmentLimit);
  }
  status = confit_input_load_toml(project_root, path, &graph->allocator, &image,
                                  diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_source_record_read(
      ledger, image,
      is_entry ? CONFIT_SOURCE_READ_ENTRY : CONFIT_SOURCE_READ_FRAGMENT,
      diagnostic);
  if (status != CONFIT_OK) {
    confit_input_image_destroy(image);
    return status;
  }
  if (!confit_input_image_identity(image, &candidate_identity) ||
      confit_input_image_bytes(image, &byte_count) == 0) {
    confit_input_image_destroy(image);
    return confit_source_fail(diagnostic, CONFIT_ERR_INTERNAL, path,
                              kInvalidArgument);
  }
  for (index = 0U; index < graph->node_count; ++index) {
    if (!confit_input_image_identity(graph->nodes[index].input,
                                     &existing_identity)) {
      confit_input_image_destroy(image);
      return confit_source_fail(diagnostic, CONFIT_ERR_INTERNAL, path,
                                kInvalidArgument);
    }
    if (confit_source_identity_equal(&candidate_identity,
                                     &existing_identity)) {
      confit_input_image_destroy(image);
      return confit_source_fail(diagnostic, CONFIT_ERR_VALIDATION, path,
                                kFileIdentityCollision);
    }
  }
  status = confit_input_image_accumulate(graph->total_bytes, image,
                                         &candidate_total, diagnostic);
  if (status != CONFIT_OK) {
    confit_input_image_destroy(image);
    return status;
  }
  status = confit_source_grow_nodes(graph, diagnostic);
  if (status != CONFIT_OK) {
    confit_input_image_destroy(image);
    return status;
  }
  index = graph->node_count;
  graph->nodes[index].input = image;
  graph->nodes[index].parent = parent;
  graph->nodes[index].source_ordinal = source_ordinal;
  graph->nodes[index].include_depth = include_depth;
  graph->nodes[index].state = CONFIT_SOURCE_NODE_VISITING;
  graph->node_count += 1U;
  graph->total_bytes = candidate_total;

  status = confit_source_load_children(graph, project_root, index, is_entry,
                                       ledger, diagnostic);
  if (status == CONFIT_OK) {
    graph->nodes[index].state = CONFIT_SOURCE_NODE_VISITED;
  }
  return status;
}

void confit_source_graph_destroy(ConfitSourceGraph *graph) {
  ConfitAllocator allocator;
  size_t index;
  if (graph == 0) {
    return;
  }
  allocator = graph->allocator;
  for (index = graph->node_count; index > 0U; --index) {
    confit_input_image_destroy(graph->nodes[index - 1U].input);
  }
  if (graph->nodes != 0) {
    allocator.deallocate(allocator.context, graph->nodes);
  }
  memset(graph, 0, sizeof(*graph));
  allocator.deallocate(allocator.context, graph);
}

ConfitStatus confit_source_graph_load_observed(
    ConfitHostRoot *project_root, const char *entry_path,
    const ConfitAllocator *allocator, ConfitSourceReadLedger *ledger,
    ConfitSourceGraph **out_graph, ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitSourceGraph *graph;
  ConfitStatus status;
  if (project_root == 0 || entry_path == 0 || out_graph == 0) {
    return confit_source_fail(diagnostic, CONFIT_ERR_USAGE, entry_path,
                              kInvalidArgument);
  }
  *out_graph = 0;
  if (!confit_source_resolve_allocator(allocator, &resolved)) {
    return confit_source_fail(diagnostic, CONFIT_ERR_USAGE, entry_path,
                              kInvalidAllocator);
  }
  graph = (ConfitSourceGraph *)resolved.allocate(resolved.context,
                                                 sizeof(*graph));
  if (graph == 0) {
    return confit_source_fail(diagnostic, CONFIT_ERR_INTERNAL, entry_path,
                              kOutOfMemory);
  }
  memset(graph, 0, sizeof(*graph));
  graph->allocator = resolved;
  status = confit_source_load_node(graph, project_root, entry_path,
                                   CONFIT_INDEX_NONE, 0U, 0U, 1, ledger,
                                   diagnostic);
  if (status != CONFIT_OK) {
    confit_source_graph_destroy(graph);
    if (diagnostic != 0) {
      diagnostic->path = entry_path;
      diagnostic->line = 0U;
      diagnostic->column = 0U;
    }
    return status;
  }
  *out_graph = graph;
  return CONFIT_OK;
}

ConfitStatus confit_source_graph_load(
    ConfitHostRoot *project_root, const char *entry_path,
    const ConfitAllocator *allocator, ConfitSourceGraph **out_graph,
    ConfitDiagnostic *diagnostic) {
  return confit_source_graph_load_observed(project_root, entry_path, allocator,
                                           0, out_graph, diagnostic);
}

size_t confit_source_graph_node_count(const ConfitSourceGraph *graph) {
  return graph != 0 ? graph->node_count : 0U;
}

size_t confit_source_graph_edge_count(const ConfitSourceGraph *graph) {
  return graph != 0 ? graph->edge_count : 0U;
}

size_t confit_source_graph_total_bytes(const ConfitSourceGraph *graph) {
  return graph != 0 ? graph->total_bytes : 0U;
}

int confit_source_graph_node_at(const ConfitSourceGraph *graph, size_t index,
                                ConfitSourceNodeView *out_view) {
  if (graph == 0 || out_view == 0 || index >= graph->node_count) {
    return 0;
  }
  out_view->path = confit_input_image_path(graph->nodes[index].input);
  out_view->parent_node = graph->nodes[index].parent;
  out_view->source_ordinal = graph->nodes[index].source_ordinal;
  out_view->include_depth = graph->nodes[index].include_depth;
  out_view->input = graph->nodes[index].input;
  return 1;
}

int confit_source_graph_find(const ConfitSourceGraph *graph, const char *path,
                             size_t *out_index) {
  if (graph == 0 || path == 0 || out_index == 0) {
    return 0;
  }
  return confit_source_find_index(graph, path, out_index);
}
