#ifndef CONFIT_RESOLVER_V2_SNAPSHOT_INTERNAL_H
#define CONFIT_RESOLVER_V2_SNAPSHOT_INTERNAL_H

#include "ledger_internal.h"

typedef struct ConfitV2ReverseNode {
  ConfitV2InvalidationNode node;
  char *owned_id;
  size_t *dependents;
  size_t dependent_count;
} ConfitV2ReverseNode;

struct ConfitV2Snapshot {
  char *project_name;
  char *project_namespace;
  char *project_version;
  char *source_root;
  char *profile_name;
  char *target_name;
  uint64_t source_hash;
  uint64_t input_hash;
  uint64_t semantic_hash;
  ConfitV2SnapshotOption *options;
  size_t option_count;
  ConfitV2SnapshotChoice *choices;
  size_t choice_count;
  ConfitV2SnapshotConstraint *constraints;
  size_t constraint_count;
  ConfitV2ProvenanceNode *provenance_nodes;
  size_t provenance_node_count;
  ConfitV2ProvenanceEdge *provenance_edges;
  size_t provenance_edge_count;
  ConfitV2ReverseNode *reverse_nodes;
  size_t reverse_node_count;
};

struct ConfitV2InvalidationSet {
  ConfitV2InvalidationNode *nodes;
  size_t count;
};

ConfitStatus confit_v2_snapshot_build_reverse_index(
    ConfitV2Snapshot *snapshot, const ConfitV2CompiledStructure *compiled,
    ConfitDiagnostic *diagnostic);
const ConfitV2ReverseNode *confit_v2_snapshot_reverse_node_find(
    const ConfitV2Snapshot *snapshot, ConfitV2InvalidationKind kind,
    const char *id, size_t *out_index);

#endif /* CONFIT_RESOLVER_V2_SNAPSHOT_INTERNAL_H */
