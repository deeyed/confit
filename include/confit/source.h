#ifndef CONFIT_SOURCE_H
#define CONFIT_SOURCE_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/input.h"
#include "confit/model.h"
#include "confit/status.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque owner of one explicit configuration-document graph. */
typedef struct ConfitSourceGraph ConfitSourceGraph;

typedef struct ConfitSourceNodeView {
  const char *path;
  size_t parent_node;
  size_t source_ordinal;
  size_t include_depth;
  const ConfitInputImage *input;
} ConfitSourceNodeView;

/**
 * @brief Load one project-root-relative entry and only its literal sources.
 *
 * The entry's top-level `source` array and each reachable fragment's optional
 * `[menu].source` array are the only graph edges.  Paths remain relative to the
 * explicit project root, including when declared by a nested fragment.  The
 * loader never enumerates a directory, expands a glob, or inspects project
 * source.  On failure no partial graph is published.
 */
ConfitStatus confit_source_graph_load(
    ConfitHostRoot *project_root, const char *entry_path,
    const ConfitAllocator *allocator, ConfitSourceGraph **out_graph,
    ConfitDiagnostic *diagnostic);

void confit_source_graph_destroy(ConfitSourceGraph *graph);

size_t confit_source_graph_node_count(const ConfitSourceGraph *graph);
size_t confit_source_graph_edge_count(const ConfitSourceGraph *graph);
size_t confit_source_graph_total_bytes(const ConfitSourceGraph *graph);

/**
 * @brief Return one DFS-preorder presentation node as a borrowed view.
 *
 * Source-array order affects this presentation order only.  Every pointer in
 * the view expires when the graph is destroyed.
 */
int confit_source_graph_node_at(const ConfitSourceGraph *graph, size_t index,
                                ConfitSourceNodeView *out_view);

/** @brief Find an exact normalized path without changing graph order. */
int confit_source_graph_find(const ConfitSourceGraph *graph, const char *path,
                             size_t *out_index);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_SOURCE_H */
