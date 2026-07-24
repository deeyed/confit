#include "snapshot_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char kInvalidSnapshotArgument[] =
    "invalid schema v2 snapshot freeze argument";
static const char kConstraintFailure[] =
    "schema v2 constraint failure prevents snapshot publish";
static const char kAllocationFailed[] =
    "failed to allocate schema v2 immutable snapshot";

static uint64_t confit_v2_snapshot_hash_bytes(uint64_t hash, const void *data,
                                               size_t size) {
  const unsigned char *bytes = (const unsigned char *)data;
  size_t index;

  for (index = 0U; index < size; ++index) {
    hash ^= (uint64_t)bytes[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static uint64_t confit_v2_snapshot_hash_u64(uint64_t hash, uint64_t value) {
  unsigned char bytes[8];
  size_t index;

  for (index = 0U; index < sizeof(bytes); ++index) {
    bytes[index] = (unsigned char)(value & UINT64_C(0xff));
    value >>= 8U;
  }
  return confit_v2_snapshot_hash_bytes(hash, bytes, sizeof(bytes));
}

static uint64_t confit_v2_snapshot_hash_string(uint64_t hash,
                                                const char *text) {
  const size_t size = text != 0 ? strlen(text) : 0U;

  hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)size);
  return size == 0U ? hash : confit_v2_snapshot_hash_bytes(hash, text, size);
}

static uint64_t confit_v2_snapshot_hash_value(uint64_t hash,
                                               const ConfitV2Value *value) {
  size_t index;

  hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)value->kind);
  switch (value->kind) {
  case CONFIT_V2_VALUE_BOOL:
    return confit_v2_snapshot_hash_u64(hash, (uint64_t)value->as.bool_value);
  case CONFIT_V2_VALUE_TRISTATE:
    return confit_v2_snapshot_hash_u64(
        hash, (uint64_t)(unsigned char)value->as.tristate_value);
  case CONFIT_V2_VALUE_INT:
    return confit_v2_snapshot_hash_u64(hash, (uint64_t)value->as.int_value);
  case CONFIT_V2_VALUE_UINT:
    return confit_v2_snapshot_hash_u64(hash, value->as.uint_value);
  case CONFIT_V2_VALUE_FLOAT: {
    uint64_t bits = 0U;
    memcpy(&bits, &value->as.float_value, sizeof(bits));
    return confit_v2_snapshot_hash_u64(hash, bits);
  }
  case CONFIT_V2_VALUE_STRING:
    return confit_v2_snapshot_hash_string(hash, value->as.string_value);
  case CONFIT_V2_VALUE_STRING_LIST:
    hash = confit_v2_snapshot_hash_u64(
        hash, (uint64_t)value->as.string_list.count);
    for (index = 0U; index < value->as.string_list.count; ++index) {
      hash = confit_v2_snapshot_hash_string(hash,
                                            value->as.string_list.items[index]);
    }
    return hash;
  case CONFIT_V2_VALUE_UNSET:
  default:
    return hash;
  }
}

static void confit_v2_snapshot_assignment_clear(
    ConfitV2SnapshotAssignment *assignment) {
  if (assignment == 0) {
    return;
  }
  confit_v2_ledger_value_clear(&assignment->value);
  free((char *)assignment->source_path);
  memset(assignment, 0, sizeof(*assignment));
}

static void confit_v2_snapshot_option_clear(ConfitV2SnapshotOption *option) {
  if (option == 0) {
    return;
  }
  free((char *)option->id);
  confit_v2_snapshot_assignment_clear(&option->requested);
  confit_v2_ledger_value_clear(&option->effective_value);
  free((char *)option->effective_source_path);
  memset(option, 0, sizeof(*option));
}

static ConfitStatus confit_v2_snapshot_copy_text(const char *source,
                                                  const char **out) {
  char *copy;

  *out = 0;
  if (source == 0) {
    return CONFIT_OK;
  }
  copy = confit_v2_ledger_strdup(source);
  if (copy == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  *out = copy;
  return CONFIT_OK;
}

static ConfitV2ProvenanceKind confit_v2_snapshot_requested_kind(
    const ConfitV2LedgerEntry *requested) {
  if (requested == 0 || requested->is_unset) {
    return CONFIT_V2_PROVENANCE_UNSET;
  }
  switch (requested->origin) {
  case CONFIT_V2_ASSIGNMENT_ORIGIN_SCHEMA_DEFAULT:
    return CONFIT_V2_PROVENANCE_SCHEMA_DEFAULT;
  case CONFIT_V2_ASSIGNMENT_ORIGIN_PROFILE:
    return CONFIT_V2_PROVENANCE_PROFILE_ASSIGNMENT;
  case CONFIT_V2_ASSIGNMENT_ORIGIN_TARGET:
    return CONFIT_V2_PROVENANCE_TARGET_ASSIGNMENT;
  case CONFIT_V2_ASSIGNMENT_ORIGIN_USER:
    return CONFIT_V2_PROVENANCE_USER_ASSIGNMENT;
  case CONFIT_V2_ASSIGNMENT_ORIGIN_UNSET:
  default:
    return CONFIT_V2_PROVENANCE_UNSET;
  }
}

static ConfitV2ProvenanceKind confit_v2_snapshot_effective_kind(
    ConfitV2EffectiveValueOrigin origin) {
  switch (origin) {
  case CONFIT_V2_EFFECTIVE_VALUE_CONDITIONAL_DEFAULT:
    return CONFIT_V2_PROVENANCE_CONDITIONAL_DEFAULT;
  case CONFIT_V2_EFFECTIVE_VALUE_DEFAULT:
    return CONFIT_V2_PROVENANCE_SCHEMA_DEFAULT;
  case CONFIT_V2_EFFECTIVE_VALUE_COMPUTED:
    return CONFIT_V2_PROVENANCE_COMPUTED;
  case CONFIT_V2_EFFECTIVE_VALUE_UNSET:
    return CONFIT_V2_PROVENANCE_UNSET;
  case CONFIT_V2_EFFECTIVE_VALUE_REQUESTED:
  default:
    return CONFIT_V2_PROVENANCE_EFFECTIVE_VALUE;
  }
}

static ConfitStatus confit_v2_snapshot_append_provenance_node(
    ConfitV2Snapshot *snapshot, ConfitV2ProvenanceKind kind,
    const char *subject_id, const char *path, size_t line, size_t column,
    size_t *out_index) {
  ConfitV2ProvenanceNode *grown;
  ConfitV2ProvenanceNode *node;

  if (snapshot->provenance_node_count ==
      SIZE_MAX / sizeof(*snapshot->provenance_nodes)) {
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2ProvenanceNode *)realloc(
      snapshot->provenance_nodes,
      (snapshot->provenance_node_count + 1U) * sizeof(*grown));
  if (grown == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  snapshot->provenance_nodes = grown;
  node = &snapshot->provenance_nodes[snapshot->provenance_node_count];
  memset(node, 0, sizeof(*node));
  node->kind = kind;
  node->source_line = line;
  node->source_column = column;
  if (confit_v2_snapshot_copy_text(subject_id, &node->subject_id) != CONFIT_OK ||
      confit_v2_snapshot_copy_text(path, &node->source_path) != CONFIT_OK) {
    free((char *)node->subject_id);
    free((char *)node->source_path);
    memset(node, 0, sizeof(*node));
    return CONFIT_ERR_INTERNAL;
  }
  if (out_index != 0) {
    *out_index = snapshot->provenance_node_count;
  }
  snapshot->provenance_node_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_snapshot_append_provenance_edge(
    ConfitV2Snapshot *snapshot, size_t from_index, size_t to_index) {
  ConfitV2ProvenanceEdge *grown;

  if (snapshot->provenance_edge_count ==
      SIZE_MAX / sizeof(*snapshot->provenance_edges)) {
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2ProvenanceEdge *)realloc(
      snapshot->provenance_edges,
      (snapshot->provenance_edge_count + 1U) * sizeof(*grown));
  if (grown == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  snapshot->provenance_edges = grown;
  snapshot->provenance_edges[snapshot->provenance_edge_count].from_index =
      from_index;
  snapshot->provenance_edges[snapshot->provenance_edge_count].to_index =
      to_index;
  snapshot->provenance_edge_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_snapshot_copy_option(
    ConfitV2SnapshotOption *out, const ConfitV2EffectiveValue *effective) {
  const ConfitV2LedgerEntry *requested = effective->requested;
  ConfitStatus status;

  memset(out, 0, sizeof(*out));
  out->id = confit_v2_ledger_strdup(effective->symbol->id);
  if (out->id == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  out->type = effective->symbol->type;
  out->write_domain = effective->symbol->write_domain;
  out->available = effective->available;
  out->visible = effective->visible;
  out->effective_is_set = effective->is_set;
  out->effective_origin = effective->origin;
  out->effective_source_line = effective->source_line;
  out->effective_source_column = effective->source_column;
  status = confit_v2_snapshot_copy_text(effective->source_path,
                                         &out->effective_source_path);
  if (status == CONFIT_OK && effective->is_set) {
    status = confit_v2_ledger_value_copy(&out->effective_value,
                                          &effective->value);
  }
  if (status != CONFIT_OK) {
    confit_v2_snapshot_option_clear(out);
    return status;
  }
  if (requested == 0) {
    return CONFIT_OK;
  }
  out->requested.is_present = 1;
  out->requested.is_set = !requested->is_unset;
  out->requested.is_unset = requested->is_unset;
  out->requested.origin = requested->origin;
  out->requested.source_line = requested->source_line;
  out->requested.source_column = requested->source_column;
  status = confit_v2_snapshot_copy_text(requested->source_path,
                                         &out->requested.source_path);
  if (status == CONFIT_OK && !requested->is_unset) {
    status = confit_v2_ledger_value_copy(&out->requested.value,
                                          &requested->value);
  }
  if (status != CONFIT_OK) {
    confit_v2_snapshot_option_clear(out);
  }
  return status;
}

static ConfitStatus confit_v2_snapshot_hash_source(
    const ConfitV2Evaluation *evaluation, uint64_t *out_hash) {
  const ConfitV2AssignmentLedger *ledger =
      confit_v2_evaluation_source(evaluation);
  const ConfitV2CompiledStructure *compiled =
      confit_v2_assignment_ledger_source(ledger);
  const ConfitV2LinkedProject *linked =
      confit_v2_compiled_structure_source(compiled);
  const ConfitV2Project *project = confit_v2_linked_project_source(linked);
  uint64_t hash = UINT64_C(1469598103934665603);
  size_t index;

  hash = confit_v2_snapshot_hash_string(hash, project->name);
  hash = confit_v2_snapshot_hash_string(hash, project->namespace_name);
  hash = confit_v2_snapshot_hash_string(hash, project->version);
  for (index = 0U; index < confit_v2_evaluation_value_count(evaluation);
       ++index) {
    const ConfitV2EffectiveValue *value =
        confit_v2_evaluation_value_at(evaluation, index);
    const ConfitV2Symbol *symbol = value->symbol;
    size_t nested;

    hash = confit_v2_snapshot_hash_string(hash, symbol->id);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)symbol->type);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)symbol->write_domain);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)symbol->required);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)symbol->user_override);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)symbol->emit_mask);
    hash = confit_v2_snapshot_hash_string(hash, symbol->computed.text);
    hash = confit_v2_snapshot_hash_string(hash, symbol->available_if.text);
    hash = confit_v2_snapshot_hash_string(hash, symbol->visible_if.text);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)symbol->default_value.is_set);
    if (symbol->default_value.is_set) {
      hash = confit_v2_snapshot_hash_value(hash, &symbol->default_value.value);
    }
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)symbol->default_count);
    for (nested = 0U; nested < symbol->default_count; ++nested) {
      hash = confit_v2_snapshot_hash_string(hash,
          symbol->defaults[nested].when.text);
      hash = confit_v2_snapshot_hash_u64(
          hash, (uint64_t)(uint32_t)symbol->defaults[nested].priority);
      hash = confit_v2_snapshot_hash_value(
          hash, &symbol->defaults[nested].assignment.value);
    }
  }
  for (index = 0U; index < confit_v2_compiled_structure_choice_count(compiled);
       ++index) {
    const ConfitV2CompiledChoice *choice =
        confit_v2_compiled_structure_choice_at(compiled, index);
    hash = confit_v2_snapshot_hash_string(hash, choice->source->id);
    hash = confit_v2_snapshot_hash_u64(hash,
        (uint64_t)choice->source->cardinality);
  }
  for (index = 0U;
       index < confit_v2_compiled_structure_constraint_count(compiled); ++index) {
    const ConfitV2CompiledConstraint *constraint =
        confit_v2_compiled_structure_constraint_at(compiled, index);
    hash = confit_v2_snapshot_hash_string(hash, constraint->source->id);
    hash = confit_v2_snapshot_hash_string(hash, constraint->source->when.text);
    hash = confit_v2_snapshot_hash_string(hash, constraint->source->require.text);
  }
  *out_hash = hash;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_snapshot_hash_input(
    const ConfitV2AssignmentLedger *ledger, uint64_t *out_hash) {
  uint64_t hash = UINT64_C(1469598103934665603);
  size_t index;

  hash = confit_v2_snapshot_hash_string(
      hash, confit_v2_assignment_ledger_profile_name(ledger));
  hash = confit_v2_snapshot_hash_string(
      hash, confit_v2_assignment_ledger_target_name(ledger));
  for (index = 0U; index < confit_v2_assignment_ledger_entry_count(ledger);
       ++index) {
    const ConfitV2LedgerEntry *entry =
        confit_v2_assignment_ledger_entry_at(ledger, index);

    hash = confit_v2_snapshot_hash_string(hash, entry->symbol->id);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)entry->origin);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)entry->domain);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)entry->is_unset);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)entry->wins);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)entry->precedence);
    hash = confit_v2_snapshot_hash_u64(hash,
                                       (uint64_t)entry->declaration_order);
    hash = confit_v2_snapshot_hash_value(hash, &entry->value);
  }
  *out_hash = hash;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_snapshot_hash_semantic(
    const ConfitV2Snapshot *snapshot, uint64_t *out_hash) {
  uint64_t hash = UINT64_C(1469598103934665603);
  size_t index;

  for (index = 0U; index < snapshot->option_count; ++index) {
    const ConfitV2SnapshotOption *option = &snapshot->options[index];
    hash = confit_v2_snapshot_hash_string(hash, option->id);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)option->available);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)option->visible);
    hash = confit_v2_snapshot_hash_u64(hash,
        (uint64_t)option->effective_is_set);
    hash = confit_v2_snapshot_hash_u64(hash,
        (uint64_t)option->effective_origin);
    if (option->effective_is_set) {
      hash = confit_v2_snapshot_hash_value(hash, &option->effective_value);
    }
  }
  for (index = 0U; index < snapshot->choice_count; ++index) {
    const ConfitV2SnapshotChoice *choice = &snapshot->choices[index];
    hash = confit_v2_snapshot_hash_string(hash, choice->id);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)choice->available);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)choice->visible);
    hash = confit_v2_snapshot_hash_u64(hash,
        (uint64_t)choice->effective_member_count);
    hash = confit_v2_snapshot_hash_string(hash, choice->selected_member_id);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)choice->origin);
  }
  for (index = 0U; index < snapshot->constraint_count; ++index) {
    const ConfitV2SnapshotConstraint *constraint = &snapshot->constraints[index];
    hash = confit_v2_snapshot_hash_string(hash, constraint->id);
    hash = confit_v2_snapshot_hash_u64(hash, (uint64_t)constraint->outcome);
  }
  *out_hash = hash;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_snapshot_copy_choices(
    ConfitV2Snapshot *snapshot, const ConfitV2Evaluation *evaluation) {
  size_t index;

  snapshot->choice_count = confit_v2_evaluation_choice_count(evaluation);
  if (snapshot->choice_count == 0U) {
    return CONFIT_OK;
  }
  snapshot->choices = (ConfitV2SnapshotChoice *)calloc(
      snapshot->choice_count, sizeof(*snapshot->choices));
  if (snapshot->choices == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < snapshot->choice_count; ++index) {
    const ConfitV2ChoiceResolution *source =
        confit_v2_evaluation_choice_at(evaluation, index);
    ConfitV2SnapshotChoice *out = &snapshot->choices[index];

    out->id = confit_v2_ledger_strdup(source->choice->source->id);
    out->available = source->available;
    out->visible = source->visible;
    out->effective_member_count = source->effective_member_count;
    out->origin = source->origin;
    if (out->id == 0 ||
        confit_v2_snapshot_copy_text(
            source->selected_member != 0 ? source->selected_member->id : 0,
            &out->selected_member_id) != CONFIT_OK) {
      return CONFIT_ERR_INTERNAL;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_snapshot_copy_constraints(
    ConfitV2Snapshot *snapshot, const ConfitV2ConstraintReport *report) {
  size_t index;

  snapshot->constraint_count =
      confit_v2_constraint_report_result_count(report);
  if (snapshot->constraint_count == 0U) {
    return CONFIT_OK;
  }
  snapshot->constraints = (ConfitV2SnapshotConstraint *)calloc(
      snapshot->constraint_count, sizeof(*snapshot->constraints));
  if (snapshot->constraints == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < snapshot->constraint_count; ++index) {
    const ConfitV2ConstraintResult *source =
        confit_v2_constraint_report_result_at(report, index);
    ConfitV2SnapshotConstraint *out = &snapshot->constraints[index];
    const ConfitV2Constraint *definition = source->constraint->source;

    out->id = confit_v2_ledger_strdup(definition->id);
    out->outcome = source->outcome;
    out->source_line = definition->span.line;
    out->source_column = definition->span.column;
    if (out->id == 0 ||
        confit_v2_snapshot_copy_text(definition->message, &out->message) !=
            CONFIT_OK ||
        confit_v2_snapshot_copy_text(definition->span.path,
                                     &out->source_path) != CONFIT_OK) {
      return CONFIT_ERR_INTERNAL;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_snapshot_build_provenance(
    ConfitV2Snapshot *snapshot, const ConfitV2Evaluation *evaluation,
    const ConfitV2ConstraintReport *report) {
  size_t *effective_nodes;
  size_t index;
  ConfitStatus status = CONFIT_OK;

  effective_nodes = (size_t *)calloc(snapshot->option_count,
                                      sizeof(*effective_nodes));
  if (snapshot->option_count > 0U && effective_nodes == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < snapshot->option_count && status == CONFIT_OK;
       ++index) {
    const ConfitV2EffectiveValue *effective =
        confit_v2_evaluation_value_at(evaluation, index);
    const ConfitV2LedgerEntry *requested = effective->requested;
    size_t cause_node;

    status = confit_v2_snapshot_append_provenance_node(
        snapshot, confit_v2_snapshot_requested_kind(requested),
        effective->symbol->id,
        requested != 0 ? requested->source_path : effective->source_path,
        requested != 0 ? requested->source_line : effective->source_line,
        requested != 0 ? requested->source_column : effective->source_column,
        &cause_node);
    if (status == CONFIT_OK &&
        effective->origin != CONFIT_V2_EFFECTIVE_VALUE_REQUESTED) {
      status = confit_v2_snapshot_append_provenance_node(
          snapshot, confit_v2_snapshot_effective_kind(effective->origin),
          effective->symbol->id, effective->source_path, effective->source_line,
          effective->source_column, &cause_node);
    }
    if (status == CONFIT_OK) {
      status = confit_v2_snapshot_append_provenance_node(
          snapshot, CONFIT_V2_PROVENANCE_EFFECTIVE_VALUE, effective->symbol->id,
          effective->source_path, effective->source_line, effective->source_column,
          &effective_nodes[index]);
    }
    if (status == CONFIT_OK) {
      status = confit_v2_snapshot_append_provenance_edge(
          snapshot, cause_node, effective_nodes[index]);
    }
  }
  for (index = 0U; index < snapshot->choice_count && status == CONFIT_OK;
       ++index) {
    size_t choice_node;
    const ConfitV2SnapshotChoice *choice = &snapshot->choices[index];
    size_t option_index;

    status = confit_v2_snapshot_append_provenance_node(
        snapshot, CONFIT_V2_PROVENANCE_CHOICE_DECISION, choice->id, 0, 0U, 0U,
        &choice_node);
    for (option_index = 0U; option_index < snapshot->option_count &&
                             status == CONFIT_OK;
         ++option_index) {
      if (choice->selected_member_id != 0 &&
          strcmp(choice->selected_member_id,
                 snapshot->options[option_index].id) == 0) {
        status = confit_v2_snapshot_append_provenance_edge(
            snapshot, choice_node, effective_nodes[option_index]);
      }
    }
  }
  for (index = 0U; index < snapshot->constraint_count && status == CONFIT_OK;
       ++index) {
    const ConfitV2ConstraintResult *result =
        confit_v2_constraint_report_result_at(report, index);
    size_t constraint_node;
    size_t read_index;

    status = confit_v2_snapshot_append_provenance_node(
        snapshot, CONFIT_V2_PROVENANCE_CONSTRAINT,
        snapshot->constraints[index].id, snapshot->constraints[index].source_path,
        snapshot->constraints[index].source_line,
        snapshot->constraints[index].source_column, &constraint_node);
    for (read_index = 0U; read_index < result->read_count && status == CONFIT_OK;
         ++read_index) {
      size_t option_index;
      for (option_index = 0U; option_index < snapshot->option_count;
           ++option_index) {
        if (strcmp(result->reads[read_index].symbol->id,
                   snapshot->options[option_index].id) == 0) {
          status = confit_v2_snapshot_append_provenance_edge(
              snapshot, effective_nodes[option_index], constraint_node);
          break;
        }
      }
    }
  }
  free(effective_nodes);
  return status;
}

ConfitStatus confit_v2_snapshot_freeze(
    const ConfitV2AssignmentLedger *ledger,
    const ConfitV2Evaluation *evaluation,
    const ConfitV2ConstraintReport *report, ConfitV2Snapshot **out_snapshot,
    ConfitDiagnostic *diagnostic) {
  ConfitV2Snapshot *snapshot;
  const ConfitV2CompiledStructure *compiled;
  ConfitStatus status;
  size_t index;

  if (out_snapshot == 0) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INVALID_ARGUMENT,
                                kInvalidSnapshotArgument, diagnostic);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_snapshot = 0;
  if (ledger == 0 || evaluation == 0 || report == 0 ||
      confit_v2_evaluation_source(evaluation) != ledger ||
      confit_v2_constraint_report_source(report) !=
          confit_v2_assignment_ledger_source(ledger)) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INVALID_ARGUMENT,
                                kInvalidSnapshotArgument, diagnostic);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (confit_v2_constraint_report_failure_count(report) != 0U) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_SCHEMA,
                                kConstraintFailure, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  snapshot = (ConfitV2Snapshot *)calloc(1U, sizeof(*snapshot));
  if (snapshot == 0) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INTERNAL,
                                kAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  snapshot->profile_name = confit_v2_ledger_strdup(
      confit_v2_assignment_ledger_profile_name(ledger));
  snapshot->target_name = confit_v2_ledger_strdup(
      confit_v2_assignment_ledger_target_name(ledger));
  if ((confit_v2_assignment_ledger_profile_name(ledger) != 0 &&
       snapshot->profile_name == 0) ||
      (confit_v2_assignment_ledger_target_name(ledger) != 0 &&
       snapshot->target_name == 0)) {
    status = CONFIT_ERR_INTERNAL;
    goto fail;
  }
  snapshot->option_count = confit_v2_evaluation_value_count(evaluation);
  if (snapshot->option_count > 0U) {
    snapshot->options = (ConfitV2SnapshotOption *)calloc(
        snapshot->option_count, sizeof(*snapshot->options));
    if (snapshot->options == 0) {
      status = CONFIT_ERR_INTERNAL;
      goto fail;
    }
  }
  for (index = 0U; index < snapshot->option_count; ++index) {
    status = confit_v2_snapshot_copy_option(
        &snapshot->options[index], confit_v2_evaluation_value_at(evaluation, index));
    if (status != CONFIT_OK) {
      goto fail;
    }
  }
  status = confit_v2_snapshot_copy_choices(snapshot, evaluation);
  if (status == CONFIT_OK) {
    status = confit_v2_snapshot_copy_constraints(snapshot, report);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_snapshot_build_provenance(snapshot, evaluation, report);
  }
  compiled = confit_v2_assignment_ledger_source(ledger);
  if (status == CONFIT_OK) {
    status = confit_v2_snapshot_build_reverse_index(snapshot, compiled, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_snapshot_hash_source(evaluation, &snapshot->source_hash);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_snapshot_hash_input(ledger, &snapshot->input_hash);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_snapshot_hash_semantic(snapshot, &snapshot->semantic_hash);
  }
  if (status != CONFIT_OK) {
    goto fail;
  }
  *out_snapshot = snapshot;
  return CONFIT_OK;

fail:
  confit_v2_snapshot_free(snapshot);
  confit_v2_ledger_diagnostic(0, 0U, 0U, status, kAllocationFailed, diagnostic);
  return status;
}

ConfitStatus confit_v2_snapshot_resolve(
    const ConfitV2CompiledStructure *compiled,
    const ConfitV2LedgerOptions *options, ConfitV2Snapshot **out_snapshot,
    ConfitDiagnostic *diagnostic) {
  ConfitV2AssignmentLedger *ledger = 0;
  ConfitV2Evaluation *evaluation = 0;
  ConfitV2ConstraintReport *report = 0;
  ConfitStatus status;

  if (compiled == 0 || out_snapshot == 0) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INVALID_ARGUMENT,
                                kInvalidSnapshotArgument, diagnostic);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_snapshot = 0;
  status = confit_v2_assignment_ledger_build(compiled, options, &ledger,
                                              diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_evaluation_build(ledger, &evaluation, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_evaluation_validate_constraints(evaluation, &report,
                                                        diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_snapshot_freeze(ledger, evaluation, report, out_snapshot,
                                       diagnostic);
  }
  confit_v2_constraint_report_free(report);
  confit_v2_evaluation_free(evaluation);
  confit_v2_assignment_ledger_free(ledger);
  return status;
}

void confit_v2_snapshot_free(ConfitV2Snapshot *snapshot) {
  size_t index;

  if (snapshot == 0) {
    return;
  }
  free(snapshot->profile_name);
  free(snapshot->target_name);
  for (index = 0U; index < snapshot->option_count; ++index) {
    confit_v2_snapshot_option_clear(&snapshot->options[index]);
  }
  for (index = 0U; index < snapshot->choice_count; ++index) {
    free((char *)snapshot->choices[index].id);
    free((char *)snapshot->choices[index].selected_member_id);
  }
  for (index = 0U; index < snapshot->constraint_count; ++index) {
    free((char *)snapshot->constraints[index].id);
    free((char *)snapshot->constraints[index].message);
    free((char *)snapshot->constraints[index].source_path);
  }
  for (index = 0U; index < snapshot->provenance_node_count; ++index) {
    free((char *)snapshot->provenance_nodes[index].subject_id);
    free((char *)snapshot->provenance_nodes[index].source_path);
  }
  for (index = 0U; index < snapshot->reverse_node_count; ++index) {
    free(snapshot->reverse_nodes[index].owned_id);
    free(snapshot->reverse_nodes[index].dependents);
  }
  free(snapshot->reverse_nodes);
  free(snapshot->provenance_edges);
  free(snapshot->provenance_nodes);
  free(snapshot->constraints);
  free(snapshot->choices);
  free(snapshot->options);
  free(snapshot);
}

uint64_t confit_v2_snapshot_source_hash(const ConfitV2Snapshot *snapshot) {
  return snapshot != 0 ? snapshot->source_hash : 0U;
}

uint64_t confit_v2_snapshot_input_hash(const ConfitV2Snapshot *snapshot) {
  return snapshot != 0 ? snapshot->input_hash : 0U;
}

uint64_t confit_v2_snapshot_semantic_hash(const ConfitV2Snapshot *snapshot) {
  return snapshot != 0 ? snapshot->semantic_hash : 0U;
}

const char *confit_v2_snapshot_profile_name(const ConfitV2Snapshot *snapshot) {
  return snapshot != 0 ? snapshot->profile_name : 0;
}

const char *confit_v2_snapshot_target_name(const ConfitV2Snapshot *snapshot) {
  return snapshot != 0 ? snapshot->target_name : 0;
}

size_t confit_v2_snapshot_option_count(const ConfitV2Snapshot *snapshot) {
  return snapshot != 0 ? snapshot->option_count : 0U;
}

const ConfitV2SnapshotOption *confit_v2_snapshot_option_at(
    const ConfitV2Snapshot *snapshot, size_t index) {
  return snapshot != 0 && index < snapshot->option_count
             ? &snapshot->options[index]
             : 0;
}

const ConfitV2SnapshotOption *confit_v2_snapshot_find_option(
    const ConfitV2Snapshot *snapshot, const char *option_id) {
  size_t low = 0U;
  size_t high;

  if (snapshot == 0 || option_id == 0) {
    return 0;
  }
  high = snapshot->option_count;
  while (low < high) {
    const size_t middle = low + (high - low) / 2U;
    const int compared = strcmp(snapshot->options[middle].id, option_id);
    if (compared < 0) {
      low = middle + 1U;
    } else {
      high = middle;
    }
  }
  return low < snapshot->option_count &&
                 strcmp(snapshot->options[low].id, option_id) == 0
             ? &snapshot->options[low]
             : 0;
}

size_t confit_v2_snapshot_choice_count(const ConfitV2Snapshot *snapshot) {
  return snapshot != 0 ? snapshot->choice_count : 0U;
}

const ConfitV2SnapshotChoice *confit_v2_snapshot_choice_at(
    const ConfitV2Snapshot *snapshot, size_t index) {
  return snapshot != 0 && index < snapshot->choice_count
             ? &snapshot->choices[index]
             : 0;
}

size_t confit_v2_snapshot_constraint_count(const ConfitV2Snapshot *snapshot) {
  return snapshot != 0 ? snapshot->constraint_count : 0U;
}

const ConfitV2SnapshotConstraint *confit_v2_snapshot_constraint_at(
    const ConfitV2Snapshot *snapshot, size_t index) {
  return snapshot != 0 && index < snapshot->constraint_count
             ? &snapshot->constraints[index]
             : 0;
}

size_t confit_v2_snapshot_provenance_node_count(
    const ConfitV2Snapshot *snapshot) {
  return snapshot != 0 ? snapshot->provenance_node_count : 0U;
}

const ConfitV2ProvenanceNode *confit_v2_snapshot_provenance_node_at(
    const ConfitV2Snapshot *snapshot, size_t index) {
  return snapshot != 0 && index < snapshot->provenance_node_count
             ? &snapshot->provenance_nodes[index]
             : 0;
}

size_t confit_v2_snapshot_provenance_edge_count(
    const ConfitV2Snapshot *snapshot) {
  return snapshot != 0 ? snapshot->provenance_edge_count : 0U;
}

const ConfitV2ProvenanceEdge *confit_v2_snapshot_provenance_edge_at(
    const ConfitV2Snapshot *snapshot, size_t index) {
  return snapshot != 0 && index < snapshot->provenance_edge_count
             ? &snapshot->provenance_edges[index]
             : 0;
}
