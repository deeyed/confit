#include "ledger_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ConfitV2EvaluationDependency {
  size_t target_index;
  const ConfitV2SourceSpan *span;
} ConfitV2EvaluationDependency;

typedef struct ConfitV2EvaluationNode {
  const ConfitV2Symbol *symbol;
  ConfitV2EvaluationDependency *dependencies;
  size_t dependency_count;
  int visit_state;
} ConfitV2EvaluationNode;

static const char kInvalidEvaluationArgument[] =
    "invalid schema v2 evaluation argument";
static const char kAllocationFailed[] =
    "failed to allocate schema v2 evaluation data";
static const char kEvaluationCycle[] = "schema v2 evaluation cycle";
static const char kConditionalDefaultUnset[] =
    "schema v2 conditional default condition is unset";
static const char kConditionalDefaultAmbiguous[] =
    "ambiguous schema v2 conditional default";
static const char kMissingComputedExpression[] =
    "computed schema v2 option has no linked expression";
static const char kComputedValueUnset[] =
    "computed schema v2 expression produced unset value";
static const char kRequiredValueUnset[] = "required schema v2 option is unset";
static const char kAvailabilityConditionUnset[] =
    "schema v2 available_if condition is unset";
static const char kVisibilityConditionUnset[] =
    "schema v2 visible_if condition is unset";
static const char kUnavailableRequestedValue[] =
    "requested schema v2 value is unavailable";
static const char kUnavailableEffectiveValue[] =
    "schema v2 effective value is unavailable";
static const char kChoiceAvailabilityConditionUnset[] =
    "schema v2 choice available_if condition is unset";
static const char kChoiceVisibilityConditionUnset[] =
    "schema v2 choice visible_if condition is unset";
static const char kChoiceDefaultConditionUnset[] =
    "schema v2 choice default condition is unset";
static const char kChoiceDefaultAmbiguous[] =
    "ambiguous schema v2 choice default";
static const char kChoiceDefaultUnavailable[] =
    "schema v2 choice default member is unavailable";
static const char kChoiceTooManySelected[] =
    "schema v2 choice has too many selected members";
static const char kChoiceRequiresSelection[] =
    "schema v2 choice requires an explicit member selection";

static int confit_v2_evaluation_symbol_compare(const void *left,
                                                const void *right) {
  const ConfitV2EvaluationNode *left_node =
      (const ConfitV2EvaluationNode *)left;
  const ConfitV2EvaluationNode *right_node =
      (const ConfitV2EvaluationNode *)right;

  return strcmp(left_node->symbol->id, right_node->symbol->id);
}

static int confit_v2_evaluation_dependency_compare(const void *left,
                                                    const void *right) {
  const ConfitV2EvaluationDependency *left_dependency =
      (const ConfitV2EvaluationDependency *)left;
  const ConfitV2EvaluationDependency *right_dependency =
      (const ConfitV2EvaluationDependency *)right;
  const char *left_path = left_dependency->span != 0
                              ? left_dependency->span->path
                              : 0;
  const char *right_path = right_dependency->span != 0
                               ? right_dependency->span->path
                               : 0;

  if (left_dependency->target_index != right_dependency->target_index) {
    return left_dependency->target_index < right_dependency->target_index ? -1
                                                                           : 1;
  }
  if (left_path == 0) {
    return right_path == 0 ? 0 : -1;
  }
  if (right_path == 0) {
    return 1;
  }
  if (strcmp(left_path, right_path) != 0) {
    return strcmp(left_path, right_path);
  }
  if (left_dependency->span->line != right_dependency->span->line) {
    return left_dependency->span->line < right_dependency->span->line ? -1 : 1;
  }
  if (left_dependency->span->column != right_dependency->span->column) {
    return left_dependency->span->column < right_dependency->span->column ? -1
                                                                            : 1;
  }
  return 0;
}

static int confit_v2_evaluation_value_equal(const ConfitV2Value *left,
                                            const ConfitV2Value *right) {
  size_t index;

  if (left->kind != right->kind) {
    return 0;
  }
  switch (left->kind) {
  case CONFIT_V2_VALUE_BOOL:
    return left->as.bool_value == right->as.bool_value;
  case CONFIT_V2_VALUE_TRISTATE:
    return left->as.tristate_value == right->as.tristate_value;
  case CONFIT_V2_VALUE_INT:
    return left->as.int_value == right->as.int_value;
  case CONFIT_V2_VALUE_UINT:
    return left->as.uint_value == right->as.uint_value;
  case CONFIT_V2_VALUE_FLOAT:
    return left->as.float_value == right->as.float_value;
  case CONFIT_V2_VALUE_STRING:
    return left->as.string_value != 0 && right->as.string_value != 0 &&
           strcmp(left->as.string_value, right->as.string_value) == 0;
  case CONFIT_V2_VALUE_STRING_LIST:
    if (left->as.string_list.count != right->as.string_list.count) {
      return 0;
    }
    for (index = 0U; index < left->as.string_list.count; ++index) {
      if (strcmp(left->as.string_list.items[index],
                 right->as.string_list.items[index]) != 0) {
        return 0;
      }
    }
    return 1;
  case CONFIT_V2_VALUE_UNSET:
  default:
    return 1;
  }
}

static size_t confit_v2_evaluation_find_node(
    const ConfitV2EvaluationNode *nodes, size_t node_count, const char *id) {
  size_t low = 0U;
  size_t high = node_count;

  while (low < high) {
    const size_t middle = low + (high - low) / 2U;
    const int compared = strcmp(nodes[middle].symbol->id, id);
    if (compared < 0) {
      low = middle + 1U;
    } else {
      high = middle;
    }
  }
  return low < node_count && strcmp(nodes[low].symbol->id, id) == 0 ? low
                                                                      : node_count;
}

static int confit_v2_evaluation_role_affects_value(
    ConfitV2LinkedExpressionRole role) {
  return role == CONFIT_V2_LINKED_EXPRESSION_COMPUTED ||
         role == CONFIT_V2_LINKED_EXPRESSION_AVAILABLE_IF ||
         role == CONFIT_V2_LINKED_EXPRESSION_DEFAULT_WHEN;
}

static const ConfitV2LinkedExpression *confit_v2_evaluation_find_expression(
    const ConfitV2LinkedProject *linked, ConfitV2LinkedExpressionRole role,
    const char *owner_id, size_t occurrence) {
  return confit_v2_linked_project_find_expression(linked, role, owner_id,
                                                   occurrence);
}

static ConfitStatus confit_v2_evaluation_append_dependency(
    ConfitV2EvaluationNode *node, size_t target_index,
    const ConfitV2SourceSpan *span, ConfitDiagnostic *diagnostic) {
  ConfitV2EvaluationDependency *grown;

  if (node->dependency_count == SIZE_MAX / sizeof(*node->dependencies)) {
    confit_v2_ledger_diagnostic(span != 0 ? span->path : 0,
                                span != 0 ? span->line : 0U,
                                span != 0 ? span->column : 0U,
                                CONFIT_ERR_INTERNAL, kAllocationFailed,
                                diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2EvaluationDependency *)realloc(
      node->dependencies,
      (node->dependency_count + 1U) * sizeof(*node->dependencies));
  if (grown == 0) {
    confit_v2_ledger_diagnostic(span != 0 ? span->path : 0,
                                span != 0 ? span->line : 0U,
                                span != 0 ? span->column : 0U,
                                CONFIT_ERR_INTERNAL, kAllocationFailed,
                                diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  node->dependencies = grown;
  node->dependencies[node->dependency_count].target_index = target_index;
  node->dependencies[node->dependency_count].span = span;
  node->dependency_count += 1U;
  return CONFIT_OK;
}

static void confit_v2_evaluation_nodes_clear(ConfitV2EvaluationNode *nodes,
                                              size_t node_count) {
  size_t index;

  for (index = 0U; index < node_count; ++index) {
    free(nodes[index].dependencies);
  }
  free(nodes);
}

static ConfitStatus confit_v2_evaluation_build_nodes(
    const ConfitV2AssignmentLedger *ledger, ConfitV2EvaluationNode **out_nodes,
    size_t *out_count, ConfitDiagnostic *diagnostic) {
  const ConfitV2LinkedProject *linked = confit_v2_compiled_structure_source(
      confit_v2_assignment_ledger_source(ledger));
  const ConfitV2Project *project = confit_v2_linked_project_source(linked);
  ConfitV2EvaluationNode *nodes;
  size_t index;

  *out_nodes = 0;
  *out_count = 0U;
  nodes = (ConfitV2EvaluationNode *)calloc(project->symbol_count,
                                            sizeof(*nodes));
  if (project->symbol_count > 0U && nodes == 0) {
    confit_v2_ledger_diagnostic(project->config_root, 0U, 0U,
                                CONFIT_ERR_INTERNAL, kAllocationFailed,
                                diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < project->symbol_count; ++index) {
    nodes[index].symbol = &project->symbols[index];
  }
  if (project->symbol_count > 1U) {
    qsort(nodes, project->symbol_count, sizeof(*nodes),
          confit_v2_evaluation_symbol_compare);
  }
  for (index = 0U; index < confit_v2_linked_project_expression_count(linked);
       ++index) {
    const ConfitV2LinkedExpression *expression =
        confit_v2_linked_project_expression_at(linked, index);
    size_t owner_index;
    size_t reference_index;

    if (!confit_v2_evaluation_role_affects_value(expression->role)) {
      continue;
    }
    owner_index = confit_v2_evaluation_find_node(
        nodes, project->symbol_count, expression->owner_id);
    if (owner_index == project->symbol_count) {
      continue;
    }
    for (reference_index = 0U; reference_index < expression->reference_count;
         ++reference_index) {
      const size_t target_index = confit_v2_evaluation_find_node(
          nodes, project->symbol_count,
          expression->references[reference_index].symbol->id);
      ConfitStatus status;

      if (target_index == project->symbol_count) {
        confit_v2_ledger_diagnostic(expression->expression->source_span.path,
                                    expression->expression->source_span.line,
                                    expression->expression->source_span.column,
                                    CONFIT_ERR_INTERNAL, kAllocationFailed,
                                    diagnostic);
        confit_v2_evaluation_nodes_clear(nodes, project->symbol_count);
        return CONFIT_ERR_INTERNAL;
      }
      status = confit_v2_evaluation_append_dependency(
          &nodes[owner_index], target_index, &expression->expression->source_span,
          diagnostic);
      if (status != CONFIT_OK) {
        confit_v2_evaluation_nodes_clear(nodes, project->symbol_count);
        return status;
      }
    }
  }
  for (index = 0U; index < project->symbol_count; ++index) {
    if (nodes[index].dependency_count > 1U) {
      qsort(nodes[index].dependencies, nodes[index].dependency_count,
            sizeof(*nodes[index].dependencies),
            confit_v2_evaluation_dependency_compare);
    }
  }
  *out_nodes = nodes;
  *out_count = project->symbol_count;
  return CONFIT_OK;
}

static size_t confit_v2_evaluation_append_text(char *buffer, size_t capacity,
                                                size_t offset, const char *text) {
  size_t index;

  if (text == 0) {
    text = "?";
  }
  for (index = 0U; text[index] != '\0' && offset + 1U < capacity; ++index) {
    buffer[offset] = text[index];
    offset += 1U;
  }
  if (capacity > 0U) {
    buffer[offset < capacity ? offset : capacity - 1U] = '\0';
  }
  return offset;
}

static void confit_v2_evaluation_cycle_diagnostic(
    const ConfitV2EvaluationNode *nodes, const size_t *path,
    const ConfitV2EvaluationDependency *const *incoming, size_t begin,
    size_t depth, const ConfitV2EvaluationDependency *closing,
    ConfitDiagnostic *diagnostic) {
  static _Thread_local char message[4096];
  size_t offset = 0U;
  size_t index;

  offset = confit_v2_evaluation_append_text(message, sizeof(message), offset,
                                             kEvaluationCycle);
  offset = confit_v2_evaluation_append_text(message, sizeof(message), offset,
                                             ": ");
  for (index = begin; index < depth; ++index) {
    const ConfitV2EvaluationDependency *edge =
        index + 1U < depth ? incoming[index + 1U] : closing;
    char location[1024];

    offset = confit_v2_evaluation_append_text(message, sizeof(message), offset,
                                               nodes[path[index]].symbol->id);
    offset = confit_v2_evaluation_append_text(message, sizeof(message), offset,
                                               " --(");
    if (edge != 0 && edge->span != 0) {
      (void)snprintf(location, sizeof(location), "%s:%zu:%zu",
                     edge->span->path != 0 ? edge->span->path : "?",
                     edge->span->line, edge->span->column);
      offset = confit_v2_evaluation_append_text(message, sizeof(message), offset,
                                                 location);
    } else {
      offset = confit_v2_evaluation_append_text(message, sizeof(message), offset,
                                                 "?");
    }
    offset = confit_v2_evaluation_append_text(message, sizeof(message), offset,
                                               ")--> ");
  }
  offset = confit_v2_evaluation_append_text(
      message, sizeof(message), offset, nodes[path[begin]].symbol->id);
  (void)offset;
  confit_v2_ledger_diagnostic(closing != 0 && closing->span != 0
                                  ? closing->span->path
                                  : 0,
                              closing != 0 && closing->span != 0
                                  ? closing->span->line
                                  : 0U,
                              closing != 0 && closing->span != 0
                                  ? closing->span->column
                                  : 0U,
                              CONFIT_ERR_SCHEMA, message, diagnostic);
}

static ConfitStatus confit_v2_evaluation_visit(
    ConfitV2EvaluationNode *nodes, size_t node_count, size_t current,
    size_t *path, const ConfitV2EvaluationDependency **incoming, size_t depth,
    size_t *out_order, size_t *in_out_order_count, ConfitDiagnostic *diagnostic) {
  size_t dependency_index;

  nodes[current].visit_state = 1;
  path[depth] = current;
  for (dependency_index = 0U;
       dependency_index < nodes[current].dependency_count; ++dependency_index) {
    const ConfitV2EvaluationDependency *dependency =
        &nodes[current].dependencies[dependency_index];
    const size_t target = dependency->target_index;
    size_t index;
    ConfitStatus status;

    if (nodes[target].visit_state == 1) {
      for (index = 0U; index < depth; ++index) {
        if (path[index] == target) {
          break;
        }
      }
      confit_v2_evaluation_cycle_diagnostic(nodes, path, incoming, index,
                                             depth + 1U, dependency, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    if (nodes[target].visit_state == 2) {
      continue;
    }
    incoming[depth + 1U] = dependency;
    status = confit_v2_evaluation_visit(nodes, node_count, target, path, incoming,
                                         depth + 1U, out_order, in_out_order_count,
                                         diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  nodes[current].visit_state = 2;
  if (*in_out_order_count >= node_count) {
    confit_v2_ledger_diagnostic(nodes[current].symbol->span.path,
                                nodes[current].symbol->span.line,
                                nodes[current].symbol->span.column,
                                CONFIT_ERR_INTERNAL, kAllocationFailed,
                                diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  out_order[*in_out_order_count] = current;
  *in_out_order_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_evaluation_topological_order(
    ConfitV2EvaluationNode *nodes, size_t node_count, size_t **out_order,
    ConfitDiagnostic *diagnostic) {
  size_t *path;
  const ConfitV2EvaluationDependency **incoming;
  size_t *order;
  size_t order_count = 0U;
  size_t index;

  *out_order = 0;
  if (node_count == 0U) {
    return CONFIT_OK;
  }
  path = (size_t *)calloc(node_count, sizeof(*path));
  incoming = (const ConfitV2EvaluationDependency **)calloc(
      node_count, sizeof(*incoming));
  order = (size_t *)calloc(node_count, sizeof(*order));
  if (path == 0 || incoming == 0 || order == 0) {
    free(path);
    free(incoming);
    free(order);
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INTERNAL,
                                kAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < node_count; ++index) {
    ConfitStatus status;
    if (nodes[index].visit_state != 0) {
      continue;
    }
    incoming[0] = 0;
    status = confit_v2_evaluation_visit(nodes, node_count, index, path, incoming,
                                         0U, order, &order_count, diagnostic);
    if (status != CONFIT_OK) {
      free(path);
      free(incoming);
      free(order);
      return status;
    }
  }
  free(path);
  free(incoming);
  if (order_count != node_count) {
    free(order);
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INTERNAL,
                                kAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  *out_order = order;
  return CONFIT_OK;
}

static void confit_v2_evaluation_value_clear(ConfitV2EffectiveValue *value) {
  if (value == 0) {
    return;
  }
  confit_v2_ledger_value_clear(&value->value);
  memset(value, 0, sizeof(*value));
}

static ConfitStatus confit_v2_evaluation_set_value(
    ConfitV2EffectiveValue *out, const ConfitV2Value *value,
    ConfitV2EffectiveValueOrigin origin, const ConfitV2LedgerEntry *requested,
    const char *source_path, size_t source_line, size_t source_column,
    ConfitDiagnostic *diagnostic) {
  if (confit_v2_ledger_value_copy(&out->value, value) != CONFIT_OK) {
    confit_v2_ledger_diagnostic(source_path, source_line, source_column,
                                CONFIT_ERR_INTERNAL, kAllocationFailed,
                                diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  if (confit_v2_ledger_validate_value(out->symbol, &out->value, source_path,
                                      source_line, source_column, diagnostic) !=
      CONFIT_OK) {
    confit_v2_ledger_value_clear(&out->value);
    return CONFIT_ERR_SCHEMA;
  }
  out->is_set = 1;
  out->origin = origin;
  out->requested = requested;
  out->source_path = source_path;
  out->source_line = source_line;
  out->source_column = source_column;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_evaluation_conditional_default(
    const ConfitV2LinkedProject *linked, const ConfitV2Symbol *symbol,
    const ConfitV2ExpressionEnvironment *environment,
    const ConfitV2ConditionalDefault **out_default, ConfitDiagnostic *diagnostic) {
  const ConfitV2ConditionalDefault *selected = 0;
  size_t index;

  *out_default = 0;
  for (index = 0U; index < symbol->default_count; ++index) {
    const ConfitV2LinkedExpression *expression =
        confit_v2_evaluation_find_expression(
            linked, CONFIT_V2_LINKED_EXPRESSION_DEFAULT_WHEN, symbol->id, index);
    ConfitV2ExpressionValue condition;
    ConfitStatus status;

    if (expression == 0) {
      confit_v2_ledger_diagnostic(symbol->defaults[index].when.span.path,
                                  symbol->defaults[index].when.span.line,
                                  symbol->defaults[index].when.span.column,
                                  CONFIT_ERR_INTERNAL, kAllocationFailed,
                                  diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
    memset(&condition, 0, sizeof(condition));
    status = confit_v2_expression_evaluate(expression->typed, environment,
                                            &condition, diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_expression_value_clear(&condition);
      return status;
    }
    if (!condition.is_set) {
      confit_v2_expression_value_clear(&condition);
      confit_v2_ledger_diagnostic(expression->expression->source_span.path,
                                  expression->expression->source_span.line,
                                  expression->expression->source_span.column,
                                  CONFIT_ERR_SCHEMA, kConditionalDefaultUnset,
                                  diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    if (condition.value.as.bool_value) {
      if (selected == 0 || symbol->defaults[index].priority > selected->priority) {
        selected = &symbol->defaults[index];
      } else if (symbol->defaults[index].priority == selected->priority &&
                 !confit_v2_evaluation_value_equal(
                     &symbol->defaults[index].assignment.value,
                     &selected->assignment.value)) {
        confit_v2_expression_value_clear(&condition);
        confit_v2_ledger_diagnostic(
            symbol->defaults[index].span.path, symbol->defaults[index].span.line,
            symbol->defaults[index].span.column, CONFIT_ERR_SCHEMA,
            kConditionalDefaultAmbiguous, diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
    }
    confit_v2_expression_value_clear(&condition);
  }
  *out_default = selected;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_evaluation_compute_symbol(
    const ConfitV2LinkedProject *linked, ConfitV2EffectiveValue *out,
    const ConfitV2ExpressionEnvironment *environment, ConfitDiagnostic *diagnostic) {
  const ConfitV2LinkedExpression *expression = confit_v2_evaluation_find_expression(
      linked, CONFIT_V2_LINKED_EXPRESSION_COMPUTED, out->symbol->id, 0U);
  ConfitV2ExpressionValue computed;
  ConfitStatus status;

  if (expression == 0) {
    confit_v2_ledger_diagnostic(out->symbol->span.path, out->symbol->span.line,
                                out->symbol->span.column, CONFIT_ERR_INTERNAL,
                                kMissingComputedExpression, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  memset(&computed, 0, sizeof(computed));
  status = confit_v2_expression_evaluate(expression->typed, environment,
                                          &computed, diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_expression_value_clear(&computed);
    return status;
  }
  if (!computed.is_set) {
    confit_v2_expression_value_clear(&computed);
    confit_v2_ledger_diagnostic(expression->expression->source_span.path,
                                expression->expression->source_span.line,
                                expression->expression->source_span.column,
                                CONFIT_ERR_SCHEMA, kComputedValueUnset,
                                diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  status = confit_v2_ledger_validate_value(
      out->symbol, &computed.value, expression->expression->source_span.path,
      expression->expression->source_span.line,
      expression->expression->source_span.column, diagnostic);
  if (status == CONFIT_OK) {
    out->value = computed.value;
    memset(&computed.value, 0, sizeof(computed.value));
    out->is_set = 1;
    out->origin = CONFIT_V2_EFFECTIVE_VALUE_COMPUTED;
    out->source_path = expression->expression->source_span.path;
    out->source_line = expression->expression->source_span.line;
    out->source_column = expression->expression->source_span.column;
  }
  confit_v2_expression_value_clear(&computed);
  return status;
}

static ConfitStatus confit_v2_evaluation_input_symbol(
    const ConfitV2LinkedProject *linked, const ConfitV2AssignmentLedger *ledger,
    ConfitV2EffectiveValue *out, const ConfitV2ExpressionEnvironment *environment,
    ConfitDiagnostic *diagnostic) {
  const ConfitV2LedgerEntry *requested = confit_v2_assignment_ledger_requested(
      ledger, out->symbol->id);
  const ConfitV2ConditionalDefault *conditional = 0;
  ConfitStatus status;

  if (requested != 0 &&
      requested->origin != CONFIT_V2_ASSIGNMENT_ORIGIN_SCHEMA_DEFAULT &&
      !requested->is_unset) {
    return confit_v2_evaluation_set_value(
        out, &requested->value, CONFIT_V2_EFFECTIVE_VALUE_REQUESTED, requested,
        requested->source_path, requested->source_line, requested->source_column,
        diagnostic);
  }
  status = confit_v2_evaluation_conditional_default(linked, out->symbol,
                                                     environment, &conditional,
                                                     diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (conditional != 0) {
    return confit_v2_evaluation_set_value(
        out, &conditional->assignment.value,
        CONFIT_V2_EFFECTIVE_VALUE_CONDITIONAL_DEFAULT, requested,
        conditional->assignment.span.path, conditional->assignment.span.line,
        conditional->assignment.span.column, diagnostic);
  }
  if (out->symbol->default_value.is_set) {
    return confit_v2_evaluation_set_value(
        out, &out->symbol->default_value.value,
        CONFIT_V2_EFFECTIVE_VALUE_DEFAULT, requested,
        out->symbol->default_value.span.path, out->symbol->default_value.span.line,
        out->symbol->default_value.span.column, diagnostic);
  }
  out->origin = CONFIT_V2_EFFECTIVE_VALUE_UNSET;
  out->requested = requested;
  if (out->symbol->required) {
    confit_v2_ledger_diagnostic(out->symbol->span.path, out->symbol->span.line,
                                out->symbol->span.column, CONFIT_ERR_SCHEMA,
                                kRequiredValueUnset, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_evaluation_boolean_condition(
    const ConfitV2LinkedExpression *expression,
    const ConfitV2ExpressionEnvironment *environment, const char *unset_message,
    int *out_value, ConfitDiagnostic *diagnostic) {
  ConfitV2ExpressionValue condition;
  ConfitStatus status;

  *out_value = 1;
  if (expression == 0) {
    return CONFIT_OK;
  }
  memset(&condition, 0, sizeof(condition));
  status = confit_v2_expression_evaluate(expression->typed, environment,
                                          &condition, diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_expression_value_clear(&condition);
    return status;
  }
  if (!condition.is_set) {
    confit_v2_expression_value_clear(&condition);
    confit_v2_ledger_diagnostic(expression->expression->source_span.path,
                                expression->expression->source_span.line,
                                expression->expression->source_span.column,
                                CONFIT_ERR_SCHEMA, unset_message, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  *out_value = condition.value.as.bool_value != 0;
  confit_v2_expression_value_clear(&condition);
  return CONFIT_OK;
}

static int confit_v2_evaluation_value_is_disabled(
    const ConfitV2EffectiveValue *value) {
  if (!value->is_set) {
    return 1;
  }
  if (value->value.kind == CONFIT_V2_VALUE_BOOL) {
    return value->value.as.bool_value == 0;
  }
  if (value->value.kind == CONFIT_V2_VALUE_TRISTATE) {
    return value->value.as.tristate_value == 'n';
  }
  return 0;
}

static int confit_v2_evaluation_value_is_selected(
    const ConfitV2EffectiveValue *value) {
  if (!value->is_set) {
    return 0;
  }
  if (value->value.kind == CONFIT_V2_VALUE_BOOL) {
    return value->value.as.bool_value != 0;
  }
  return value->value.kind == CONFIT_V2_VALUE_TRISTATE &&
         (value->value.as.tristate_value == 'm' ||
          value->value.as.tristate_value == 'y');
}

static const ConfitV2LinkedExpression *
confit_v2_evaluation_symbol_expression(const ConfitV2LinkedProject *linked,
                                       const ConfitV2Symbol *symbol,
                                       ConfitV2LinkedExpressionRole role) {
  if ((role == CONFIT_V2_LINKED_EXPRESSION_AVAILABLE_IF &&
       symbol->available_if.text == 0) ||
      (role == CONFIT_V2_LINKED_EXPRESSION_VISIBLE_IF &&
       symbol->visible_if.text == 0)) {
    return 0;
  }
  return confit_v2_evaluation_find_expression(linked, role, symbol->id, 0U);
}

static ConfitStatus confit_v2_evaluation_option_states(
    const ConfitV2LinkedProject *linked, ConfitV2Evaluation *evaluation,
    const ConfitV2ExpressionEnvironment *environment,
    ConfitDiagnostic *diagnostic) {
  size_t index;

  for (index = 0U; index < evaluation->value_count; ++index) {
    ConfitV2EffectiveValue *value = &evaluation->values[index];
    const ConfitV2LinkedExpression *available_expression =
        confit_v2_evaluation_symbol_expression(
            linked, value->symbol, CONFIT_V2_LINKED_EXPRESSION_AVAILABLE_IF);
    const ConfitV2LinkedExpression *visible_expression;
    ConfitStatus status;

    status = confit_v2_evaluation_boolean_condition(
        available_expression, environment, kAvailabilityConditionUnset,
        &value->available, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    if (!value->available && !confit_v2_evaluation_value_is_disabled(value)) {
      const char *message = value->origin == CONFIT_V2_EFFECTIVE_VALUE_REQUESTED
                                ? kUnavailableRequestedValue
                                : kUnavailableEffectiveValue;
      confit_v2_ledger_diagnostic(value->source_path, value->source_line,
                                  value->source_column, CONFIT_ERR_SCHEMA,
                                  message, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    visible_expression = confit_v2_evaluation_symbol_expression(
        linked, value->symbol, CONFIT_V2_LINKED_EXPRESSION_VISIBLE_IF);
    status = confit_v2_evaluation_boolean_condition(
        visible_expression, environment, kVisibilityConditionUnset,
        &value->visible, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_evaluation_select_choice_default(
    const ConfitV2CompiledChoice *choice,
    const ConfitV2ExpressionEnvironment *environment,
    const ConfitV2Symbol **out_member, ConfitDiagnostic *diagnostic) {
  const ConfitV2ChoiceDefault *selected = 0;
  size_t index;

  *out_member = 0;
  for (index = 0U; index < choice->default_count; ++index) {
    ConfitV2ExpressionValue condition;
    ConfitStatus status;

    memset(&condition, 0, sizeof(condition));
    status = confit_v2_expression_evaluate(choice->default_when[index]->typed,
                                            environment, &condition, diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_expression_value_clear(&condition);
      return status;
    }
    if (!condition.is_set) {
      confit_v2_expression_value_clear(&condition);
      confit_v2_ledger_diagnostic(
          choice->default_when[index]->expression->source_span.path,
          choice->default_when[index]->expression->source_span.line,
          choice->default_when[index]->expression->source_span.column,
          CONFIT_ERR_SCHEMA, kChoiceDefaultConditionUnset, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    if (condition.value.as.bool_value) {
      const ConfitV2ChoiceDefault *candidate = &choice->source->defaults[index];
      if (selected == 0 || candidate->priority > selected->priority) {
        selected = candidate;
      } else if (candidate->priority == selected->priority &&
                 strcmp(candidate->member, selected->member) != 0) {
        confit_v2_expression_value_clear(&condition);
        confit_v2_ledger_diagnostic(candidate->span.path, candidate->span.line,
                                    candidate->span.column, CONFIT_ERR_SCHEMA,
                                    kChoiceDefaultAmbiguous, diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
    }
    confit_v2_expression_value_clear(&condition);
  }
  if (selected != 0) {
    for (index = 0U; index < choice->member_count; ++index) {
      if (strcmp(choice->members[index]->id, selected->member) == 0) {
        *out_member = choice->members[index];
        return CONFIT_OK;
      }
    }
    confit_v2_ledger_diagnostic(selected->span.path, selected->span.line,
                                selected->span.column, CONFIT_ERR_INTERNAL,
                                kAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_evaluation_one_choice(
    const ConfitV2CompiledChoice *choice, ConfitV2Evaluation *evaluation,
    const ConfitV2ExpressionEnvironment *environment,
    ConfitV2ChoiceResolution *out, ConfitDiagnostic *diagnostic) {
  const ConfitV2LinkedExpression *expression = choice->available_if;
  size_t index;
  ConfitStatus status;

  out->choice = choice;
  out->available = 1;
  out->visible = 1;
  status = confit_v2_evaluation_boolean_condition(
      expression, environment, kChoiceAvailabilityConditionUnset,
      &out->available, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_evaluation_boolean_condition(
      choice->visible_if, environment, kChoiceVisibilityConditionUnset,
      &out->visible, diagnostic);
  if (status != CONFIT_OK || !out->available) {
    return status;
  }
  for (index = 0U; index < choice->member_count; ++index) {
    const ConfitV2EffectiveValue *member =
        confit_v2_evaluation_find(evaluation, choice->members[index]->id);
    if (member == 0) {
      confit_v2_ledger_diagnostic(choice->source->span.path,
                                  choice->source->span.line,
                                  choice->source->span.column,
                                  CONFIT_ERR_INTERNAL, kAllocationFailed,
                                  diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
    if (member->available && confit_v2_evaluation_value_is_selected(member)) {
      out->effective_member_count += 1U;
      if (out->effective_member_count == 1U) {
        out->selected_member = member->symbol;
        out->origin = CONFIT_V2_CHOICE_SELECTION_EFFECTIVE_MEMBER;
      }
    }
  }
  if (out->effective_member_count == 0U) {
    const ConfitV2Symbol *default_member = 0;

    status = confit_v2_evaluation_select_choice_default(
        choice, environment, &default_member, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    if (default_member != 0) {
      const ConfitV2EffectiveValue *effective =
          confit_v2_evaluation_find(evaluation, default_member->id);
      if (effective == 0 || !effective->available) {
        confit_v2_ledger_diagnostic(choice->source->span.path,
                                    choice->source->span.line,
                                    choice->source->span.column,
                                    CONFIT_ERR_SCHEMA, kChoiceDefaultUnavailable,
                                    diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
      out->selected_member = default_member;
      out->origin = CONFIT_V2_CHOICE_SELECTION_DEFAULT;
    }
  }
  if ((choice->source->cardinality == CONFIT_V2_CHOICE_CARDINALITY_EXACTLY_ONE ||
       choice->source->cardinality == CONFIT_V2_CHOICE_CARDINALITY_ZERO_OR_ONE) &&
      out->effective_member_count > 1U) {
    confit_v2_ledger_diagnostic(choice->source->span.path,
                                choice->source->span.line,
                                choice->source->span.column,
                                CONFIT_ERR_SCHEMA, kChoiceTooManySelected,
                                diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  if ((choice->source->cardinality == CONFIT_V2_CHOICE_CARDINALITY_EXACTLY_ONE ||
       choice->source->cardinality == CONFIT_V2_CHOICE_CARDINALITY_ONE_OR_MORE) &&
      out->selected_member == 0) {
    confit_v2_ledger_diagnostic(choice->source->span.path,
                                choice->source->span.line,
                                choice->source->span.column,
                                CONFIT_ERR_SCHEMA, kChoiceRequiresSelection,
                                diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_evaluation_choices(
    const ConfitV2AssignmentLedger *ledger, ConfitV2Evaluation *evaluation,
    const ConfitV2ExpressionEnvironment *environment,
    ConfitDiagnostic *diagnostic) {
  const ConfitV2CompiledStructure *compiled =
      confit_v2_assignment_ledger_source(ledger);
  const size_t count = confit_v2_compiled_structure_choice_count(compiled);
  size_t index;

  evaluation->choice_count = count;
  if (count == 0U) {
    return CONFIT_OK;
  }
  evaluation->choices = (ConfitV2ChoiceResolution *)calloc(
      count, sizeof(*evaluation->choices));
  if (evaluation->choices == 0) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INTERNAL,
                                kAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < count; ++index) {
    ConfitStatus status = confit_v2_evaluation_one_choice(
        confit_v2_compiled_structure_choice_at(compiled, index), evaluation,
        environment, &evaluation->choices[index], diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static uint64_t confit_v2_evaluation_hash_bytes(uint64_t hash, const void *data,
                                                 size_t size) {
  const unsigned char *bytes = (const unsigned char *)data;
  size_t index;

  for (index = 0U; index < size; ++index) {
    hash ^= (uint64_t)bytes[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static uint64_t confit_v2_evaluation_hash_u64(uint64_t hash, uint64_t value) {
  unsigned char bytes[8];
  size_t index;

  for (index = 0U; index < sizeof(bytes); ++index) {
    bytes[index] = (unsigned char)(value & UINT64_C(0xff));
    value >>= 8U;
  }
  return confit_v2_evaluation_hash_bytes(hash, bytes, sizeof(bytes));
}

static uint64_t confit_v2_evaluation_hash_string(uint64_t hash,
                                                  const char *text) {
  const size_t size = text != 0 ? strlen(text) : 0U;

  hash = confit_v2_evaluation_hash_u64(hash, (uint64_t)size);
  return size > 0U ? confit_v2_evaluation_hash_bytes(hash, text, size) : hash;
}

static uint64_t confit_v2_evaluation_hash_value(uint64_t hash,
                                                 const ConfitV2Value *value) {
  size_t index;

  hash = confit_v2_evaluation_hash_u64(hash, (uint64_t)value->kind);
  switch (value->kind) {
  case CONFIT_V2_VALUE_BOOL:
    return confit_v2_evaluation_hash_u64(hash, (uint64_t)value->as.bool_value);
  case CONFIT_V2_VALUE_TRISTATE:
    return confit_v2_evaluation_hash_u64(
        hash, (uint64_t)(unsigned char)value->as.tristate_value);
  case CONFIT_V2_VALUE_INT:
    return confit_v2_evaluation_hash_u64(hash, (uint64_t)value->as.int_value);
  case CONFIT_V2_VALUE_UINT:
    return confit_v2_evaluation_hash_u64(hash, value->as.uint_value);
  case CONFIT_V2_VALUE_FLOAT: {
    uint64_t bits = 0U;
    memcpy(&bits, &value->as.float_value, sizeof(bits));
    return confit_v2_evaluation_hash_u64(hash, bits);
  }
  case CONFIT_V2_VALUE_STRING:
    return confit_v2_evaluation_hash_string(hash, value->as.string_value);
  case CONFIT_V2_VALUE_STRING_LIST:
    hash = confit_v2_evaluation_hash_u64(
        hash, (uint64_t)value->as.string_list.count);
    for (index = 0U; index < value->as.string_list.count; ++index) {
      hash = confit_v2_evaluation_hash_string(hash,
                                              value->as.string_list.items[index]);
    }
    return hash;
  case CONFIT_V2_VALUE_UNSET:
  default:
    return hash;
  }
}

ConfitStatus confit_v2_evaluation_build(const ConfitV2AssignmentLedger *ledger,
                                         ConfitV2Evaluation **out_evaluation,
                                         ConfitDiagnostic *diagnostic) {
  const ConfitV2LinkedProject *linked;
  ConfitV2EvaluationNode *nodes = 0;
  ConfitV2Evaluation *evaluation = 0;
  ConfitV2ExpressionBinding *bindings = 0;
  ConfitV2ExpressionEnvironment environment;
  size_t *order = 0;
  size_t node_count = 0U;
  size_t order_index;
  ConfitStatus status;

  if (ledger == 0 || out_evaluation == 0) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INVALID_ARGUMENT,
                                kInvalidEvaluationArgument, diagnostic);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_evaluation = 0;
  linked = confit_v2_compiled_structure_source(
      confit_v2_assignment_ledger_source(ledger));
  status = confit_v2_evaluation_build_nodes(ledger, &nodes, &node_count,
                                            diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_evaluation_topological_order(nodes, node_count, &order,
                                                   diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_evaluation_nodes_clear(nodes, node_count);
    return status;
  }
  evaluation = (ConfitV2Evaluation *)calloc(1U, sizeof(*evaluation));
  if (evaluation == 0) {
    status = CONFIT_ERR_INTERNAL;
    confit_v2_ledger_diagnostic(0, 0U, 0U, status, kAllocationFailed,
                                diagnostic);
    goto fail;
  }
  evaluation->ledger = ledger;
  evaluation->value_count = node_count;
  if (node_count > 0U) {
    evaluation->values = (ConfitV2EffectiveValue *)calloc(
        node_count, sizeof(*evaluation->values));
    bindings = (ConfitV2ExpressionBinding *)calloc(node_count, sizeof(*bindings));
    if (evaluation->values == 0 || bindings == 0) {
      status = CONFIT_ERR_INTERNAL;
      confit_v2_ledger_diagnostic(0, 0U, 0U, status, kAllocationFailed,
                                  diagnostic);
      goto fail;
    }
  }
  for (order_index = 0U; order_index < node_count; ++order_index) {
    evaluation->values[order_index].symbol = nodes[order_index].symbol;
    bindings[order_index].id = nodes[order_index].symbol->id;
    bindings[order_index].type = confit_v2_expression_type_from_option_type(
        nodes[order_index].symbol->type, nodes[order_index].symbol->id);
  }
  environment.bindings = bindings;
  environment.binding_count = node_count;
  for (order_index = 0U; order_index < node_count; ++order_index) {
    const size_t node_index = order[order_index];
    ConfitV2EffectiveValue *value = &evaluation->values[node_index];

    if (value->symbol->write_domain == CONFIT_V2_WRITE_DOMAIN_COMPUTED) {
      status = confit_v2_evaluation_compute_symbol(linked, value, &environment,
                                                    diagnostic);
    } else {
      status = confit_v2_evaluation_input_symbol(linked, ledger, value,
                                                  &environment, diagnostic);
    }
    if (status != CONFIT_OK) {
      goto fail;
    }
    bindings[node_index].value = value->is_set ? &value->value : 0;
  }
  status = confit_v2_evaluation_option_states(linked, evaluation, &environment,
                                              diagnostic);
  if (status != CONFIT_OK) {
    goto fail;
  }
  status = confit_v2_evaluation_choices(ledger, evaluation, &environment,
                                        diagnostic);
  if (status != CONFIT_OK) {
    goto fail;
  }
  free(bindings);
  free(order);
  confit_v2_evaluation_nodes_clear(nodes, node_count);
  *out_evaluation = evaluation;
  return CONFIT_OK;

fail:
  free(bindings);
  free(order);
  confit_v2_evaluation_nodes_clear(nodes, node_count);
  confit_v2_evaluation_free(evaluation);
  return status;
}

void confit_v2_evaluation_free(ConfitV2Evaluation *evaluation) {
  size_t index;

  if (evaluation == 0) {
    return;
  }
  for (index = 0U; index < evaluation->value_count; ++index) {
    confit_v2_evaluation_value_clear(&evaluation->values[index]);
  }
  free(evaluation->choices);
  free(evaluation->values);
  free(evaluation);
}

const ConfitV2AssignmentLedger *confit_v2_evaluation_source(
    const ConfitV2Evaluation *evaluation) {
  return evaluation != 0 ? evaluation->ledger : 0;
}

size_t confit_v2_evaluation_value_count(const ConfitV2Evaluation *evaluation) {
  return evaluation != 0 ? evaluation->value_count : 0U;
}

const ConfitV2EffectiveValue *confit_v2_evaluation_value_at(
    const ConfitV2Evaluation *evaluation, size_t index) {
  if (evaluation == 0 || index >= evaluation->value_count) {
    return 0;
  }
  return &evaluation->values[index];
}

const ConfitV2EffectiveValue *confit_v2_evaluation_find(
    const ConfitV2Evaluation *evaluation, const char *option_id) {
  size_t low = 0U;
  size_t high;

  if (evaluation == 0 || option_id == 0) {
    return 0;
  }
  high = evaluation->value_count;
  while (low < high) {
    const size_t middle = low + (high - low) / 2U;
    const int compared = strcmp(evaluation->values[middle].symbol->id, option_id);
    if (compared < 0) {
      low = middle + 1U;
    } else {
      high = middle;
    }
  }
  return low < evaluation->value_count &&
                 strcmp(evaluation->values[low].symbol->id, option_id) == 0
             ? &evaluation->values[low]
             : 0;
}

size_t confit_v2_evaluation_choice_count(const ConfitV2Evaluation *evaluation) {
  return evaluation != 0 ? evaluation->choice_count : 0U;
}

const ConfitV2ChoiceResolution *confit_v2_evaluation_choice_at(
    const ConfitV2Evaluation *evaluation, size_t index) {
  if (evaluation == 0 || index >= evaluation->choice_count) {
    return 0;
  }
  return &evaluation->choices[index];
}

const ConfitV2ChoiceResolution *confit_v2_evaluation_find_choice(
    const ConfitV2Evaluation *evaluation, const char *choice_id) {
  size_t low = 0U;
  size_t high;

  if (evaluation == 0 || choice_id == 0) {
    return 0;
  }
  high = evaluation->choice_count;
  while (low < high) {
    const size_t middle = low + (high - low) / 2U;
    const int compared =
        strcmp(evaluation->choices[middle].choice->source->id, choice_id);
    if (compared < 0) {
      low = middle + 1U;
    } else {
      high = middle;
    }
  }
  return low < evaluation->choice_count &&
                 strcmp(evaluation->choices[low].choice->source->id,
                        choice_id) == 0
             ? &evaluation->choices[low]
             : 0;
}

ConfitStatus confit_v2_evaluation_validate_constraints(
    const ConfitV2Evaluation *evaluation,
    ConfitV2ConstraintReport **out_report, ConfitDiagnostic *diagnostic) {
  ConfitV2ConstraintBinding *bindings;
  ConfitStatus status;
  size_t index;

  if (evaluation == 0 || out_report == 0) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INVALID_ARGUMENT,
                                "invalid schema v2 constraint evaluation argument",
                                diagnostic);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_report = 0;
  bindings = (ConfitV2ConstraintBinding *)calloc(
      evaluation->value_count, sizeof(*bindings));
  if (evaluation->value_count > 0U && bindings == 0) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INTERNAL,
                                kAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < evaluation->value_count; ++index) {
    const ConfitV2EffectiveValue *value = &evaluation->values[index];

    bindings[index].symbol = value->symbol;
    bindings[index].value = value->is_set ? &value->value : 0;
    bindings[index].is_set = value->is_set;
    bindings[index].source_path = value->source_path;
    bindings[index].source_line = value->source_line;
    bindings[index].source_column = value->source_column;
  }
  status = confit_v2_constraint_validate(
      confit_v2_assignment_ledger_source(evaluation->ledger), bindings,
      evaluation->value_count, out_report, diagnostic);
  free(bindings);
  return status;
}

ConfitStatus confit_v2_evaluation_hash(const ConfitV2Evaluation *evaluation,
                                        uint64_t *out_hash) {
  uint64_t hash = UINT64_C(1469598103934665603);
  size_t index;

  if (evaluation == 0 || out_hash == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  for (index = 0U; index < evaluation->value_count; ++index) {
    const ConfitV2EffectiveValue *value = &evaluation->values[index];

    hash = confit_v2_evaluation_hash_string(hash, value->symbol->id);
    hash = confit_v2_evaluation_hash_u64(hash, (uint64_t)value->is_set);
    hash = confit_v2_evaluation_hash_u64(hash, (uint64_t)value->origin);
    if (value->is_set) {
      hash = confit_v2_evaluation_hash_value(hash, &value->value);
    }
  }
  *out_hash = hash;
  return CONFIT_OK;
}
