#include "confit/link_v2.h"

#include <stdlib.h>
#include <string.h>

struct ConfitV2LinkedProject {
  const ConfitV2Project *project;
  const ConfitV2Symbol **symbols;
  ConfitV2ExpressionBinding *bindings;
  size_t symbol_count;
  ConfitV2LinkedExpression *expressions;
  size_t expression_count;
  size_t expression_capacity;
  const ConfitV2LinkedExpression **expression_index;
};

static const char kInvalidLinkArgument[] = "invalid schema v2 linker argument";
static const char kLinkAllocationFailed[] = "failed to allocate schema v2 link data";
static const char kDuplicateLinkedSymbol[] = "duplicate canonical schema v2 symbol";
static const char kInvalidLinkedNamespace[] =
    "linked schema v2 symbol is outside project namespace";
static const char kExpressionTypeMismatch[] =
    "linked expression result type does not match declaration";
static const char kSelfReference[] = "self reference in schema v2 expression";
static const char kMissingLinkedReference[] =
    "linked expression reference is missing canonical symbol";
static const char kUnknownWriteOption[] = "write request references unknown schema v2 option";
static const char kWriteDomainViolation[] = "schema v2 write domain violation";
static const char kComputedExternalWrite[] =
    "computed schema v2 option rejects external assignment";

static void confit_v2_link_diagnostic(const ConfitV2Expression *expression,
                                      const ConfitV2ExpressionNode *node,
                                      ConfitStatus status, const char *message,
                                      ConfitDiagnostic *diagnostic) {
  const char *text = expression->source;
  size_t line = expression->source_span.line;
  size_t column = expression->source_span.column;
  size_t cursor;
  const size_t offset = node != 0 ? node->start_offset : 0U;

  for (cursor = 0U; text != 0 && cursor < offset && text[cursor] != '\0';
       ++cursor) {
    if (text[cursor] == '\n') {
      line += 1U;
      column = 1U;
    } else if (column != 0U) {
      column += 1U;
    }
  }
  confit_diagnostic_set(diagnostic, status, expression->source_span.path, line,
                        column, message);
}

static void confit_v2_link_source_diagnostic(const ConfitV2SourceSpan *span,
                                              ConfitStatus status,
                                              const char *message,
                                              ConfitDiagnostic *diagnostic) {
  confit_diagnostic_set(diagnostic, status, span != 0 ? span->path : 0,
                        span != 0 ? span->line : 0U,
                        span != 0 ? span->column : 0U, message);
}

static int confit_v2_link_symbol_compare(const void *left, const void *right) {
  const ConfitV2Symbol *const *left_symbol = (const ConfitV2Symbol *const *)left;
  const ConfitV2Symbol *const *right_symbol = (const ConfitV2Symbol *const *)right;

  return strcmp((*left_symbol)->id, (*right_symbol)->id);
}

static int confit_v2_link_expression_compare(const void *left,
                                              const void *right) {
  const ConfitV2LinkedExpression *const *left_expression =
      (const ConfitV2LinkedExpression *const *)left;
  const ConfitV2LinkedExpression *const *right_expression =
      (const ConfitV2LinkedExpression *const *)right;
  int comparison;

  if ((*left_expression)->role != (*right_expression)->role) {
    return (*left_expression)->role < (*right_expression)->role ? -1 : 1;
  }
  comparison = strcmp((*left_expression)->owner_id, (*right_expression)->owner_id);
  if (comparison != 0) {
    return comparison;
  }
  if ((*left_expression)->source_order == (*right_expression)->source_order) {
    return 0;
  }
  return (*left_expression)->source_order < (*right_expression)->source_order
             ? -1
             : 1;
}

static int confit_v2_link_symbol_is_owned_by_namespace(
    const ConfitV2Project *project, const ConfitV2Symbol *symbol) {
  const size_t namespace_size = strlen(project->namespace_name);

  return strncmp(symbol->id, project->namespace_name, namespace_size) == 0 &&
         symbol->id[namespace_size] == '.';
}

static const ConfitV2Symbol *confit_v2_link_find_symbol(
    const ConfitV2LinkedProject *linked, const char *id) {
  size_t low = 0U;
  size_t high;

  if (linked == 0 || id == 0) {
    return 0;
  }
  high = linked->symbol_count;
  while (low < high) {
    const size_t middle = low + (high - low) / 2U;
    const int comparison = strcmp(id, linked->symbols[middle]->id);
    if (comparison == 0) {
      return linked->symbols[middle];
    }
    if (comparison < 0) {
      high = middle;
    } else {
      low = middle + 1U;
    }
  }
  return 0;
}

static ConfitStatus confit_v2_link_append_reference(
    ConfitV2LinkedExpression *expression, const ConfitV2ExpressionNode *node,
    const ConfitV2Symbol *symbol, const ConfitV2Expression *parsed,
    ConfitDiagnostic *diagnostic) {
  ConfitV2LinkedReference *grown;
  size_t count;

  count = expression->reference_count;
  if (count == SIZE_MAX / sizeof(*grown)) {
    confit_v2_link_diagnostic(parsed, node, CONFIT_ERR_INTERNAL,
                              kLinkAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2LinkedReference *)realloc(
      (ConfitV2LinkedReference *)expression->references,
      (count + 1U) * sizeof(*grown));
  if (grown == 0) {
    confit_v2_link_diagnostic(parsed, node, CONFIT_ERR_INTERNAL,
                              kLinkAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  expression->references = grown;
  grown[count].node = node;
  grown[count].symbol = symbol;
  expression->reference_count = count + 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_link_collect_references(
    const ConfitV2LinkedProject *linked, ConfitV2LinkedExpression *expression,
    const ConfitV2ExpressionNode *node, const char *self_id,
    ConfitDiagnostic *diagnostic) {
  size_t index;
  ConfitStatus status;

  if (node == 0) {
    return CONFIT_OK;
  }
  if (node->kind == CONFIT_V2_EXPRESSION_NODE_REFERENCE) {
    const ConfitV2Symbol *symbol =
        confit_v2_link_find_symbol(linked, node->as.reference.option_id);
    if (symbol == 0) {
      confit_v2_link_diagnostic(expression->expression, node, CONFIT_ERR_SCHEMA,
                                kMissingLinkedReference, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    if (self_id != 0 && strcmp(self_id, symbol->id) == 0) {
      confit_v2_link_diagnostic(expression->expression, node, CONFIT_ERR_SCHEMA,
                                kSelfReference, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    return confit_v2_link_append_reference(expression, node, symbol,
                                           expression->expression, diagnostic);
  }
  switch (node->kind) {
  case CONFIT_V2_EXPRESSION_NODE_UNARY:
    return confit_v2_link_collect_references(linked, expression,
                                              node->as.unary.operand, self_id,
                                              diagnostic);
  case CONFIT_V2_EXPRESSION_NODE_BINARY:
    status = confit_v2_link_collect_references(linked, expression,
                                                node->as.binary.left, self_id,
                                                diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    return confit_v2_link_collect_references(linked, expression,
                                              node->as.binary.right, self_id,
                                              diagnostic);
  case CONFIT_V2_EXPRESSION_NODE_CONDITIONAL:
    status = confit_v2_link_collect_references(
        linked, expression, node->as.conditional.condition, self_id, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    status = confit_v2_link_collect_references(
        linked, expression, node->as.conditional.when_true, self_id, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    return confit_v2_link_collect_references(
        linked, expression, node->as.conditional.when_false, self_id, diagnostic);
  case CONFIT_V2_EXPRESSION_NODE_CALL:
    for (index = 0U; index < node->as.call.argument_count; ++index) {
      status = confit_v2_link_collect_references(linked, expression,
                                                  node->as.call.arguments[index],
                                                  self_id, diagnostic);
      if (status != CONFIT_OK) {
        return status;
      }
    }
    return CONFIT_OK;
  case CONFIT_V2_EXPRESSION_NODE_LIST:
    for (index = 0U; index < node->as.list.item_count; ++index) {
      status = confit_v2_link_collect_references(linked, expression,
                                                  node->as.list.items[index],
                                                  self_id, diagnostic);
      if (status != CONFIT_OK) {
        return status;
      }
    }
    return CONFIT_OK;
  default:
    return CONFIT_OK;
  }
}

static ConfitStatus confit_v2_link_append_expression(
    ConfitV2LinkedProject *linked, ConfitV2LinkedExpressionRole role,
    const char *owner_id, const char *self_id,
    const ConfitV2ExpressionText *source,
    const ConfitV2ExpressionType *expected_type, ConfitDiagnostic *diagnostic) {
  ConfitV2LinkedExpression *grown;
  ConfitV2LinkedExpression *record;
  ConfitStatus status;
  ConfitV2Expression *parsed = 0;
  ConfitV2TypedExpression *typed = 0;
  ConfitV2ExpressionEnvironment environment;
  size_t count;

  if (source == 0 || source->text == 0) {
    return CONFIT_OK;
  }
  count = linked->expression_count;
  status = confit_v2_expression_parse(source, 0, &parsed, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  environment.bindings = linked->bindings;
  environment.binding_count = linked->symbol_count;
  status = confit_v2_expression_type_check(parsed, &environment, &typed,
                                            diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_expression_free(parsed);
    return status;
  }
  if (!confit_v2_expression_type_equal(&typed->root_type, expected_type)) {
    confit_v2_link_diagnostic(parsed, parsed->root, CONFIT_ERR_SCHEMA,
                              kExpressionTypeMismatch, diagnostic);
    confit_v2_typed_expression_free(typed);
    confit_v2_expression_free(parsed);
    return CONFIT_ERR_SCHEMA;
  }
  if (count == linked->expression_capacity) {
    size_t capacity = linked->expression_capacity == 0U
                          ? 16U
                          : linked->expression_capacity * 2U;

    if (capacity < linked->expression_capacity ||
        capacity > SIZE_MAX / sizeof(*grown)) {
      confit_v2_link_diagnostic(parsed, parsed->root, CONFIT_ERR_INTERNAL,
                                kLinkAllocationFailed, diagnostic);
      confit_v2_typed_expression_free(typed);
      confit_v2_expression_free(parsed);
      return CONFIT_ERR_INTERNAL;
    }
    grown = (ConfitV2LinkedExpression *)realloc(
        linked->expressions, capacity * sizeof(*grown));
    if (grown == 0) {
      confit_v2_link_diagnostic(parsed, parsed->root, CONFIT_ERR_INTERNAL,
                                kLinkAllocationFailed, diagnostic);
      confit_v2_typed_expression_free(typed);
      confit_v2_expression_free(parsed);
      return CONFIT_ERR_INTERNAL;
    }
    linked->expressions = grown;
    linked->expression_capacity = capacity;
  }
  record = &linked->expressions[count];
  memset(record, 0, sizeof(*record));
  record->role = role;
  record->owner_id = owner_id;
  record->source_order = count;
  record->expression = parsed;
  record->typed = typed;
  status = confit_v2_link_collect_references(linked, record, parsed->root,
                                              self_id, diagnostic);
  if (status != CONFIT_OK) {
    free((ConfitV2LinkedReference *)record->references);
    confit_v2_typed_expression_free(typed);
    confit_v2_expression_free(parsed);
    memset(record, 0, sizeof(*record));
    return status;
  }
  linked->expression_count = count + 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_link_build_expression_index(
    ConfitV2LinkedProject *linked, ConfitDiagnostic *diagnostic) {
  size_t index;

  if (linked->expression_count == 0U) {
    return CONFIT_OK;
  }
  linked->expression_index = (const ConfitV2LinkedExpression **)calloc(
      linked->expression_count, sizeof(*linked->expression_index));
  if (linked->expression_index == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL,
                          linked->project->config_root, 0U, 0U,
                          kLinkAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < linked->expression_count; ++index) {
    linked->expression_index[index] = &linked->expressions[index];
  }
  qsort(linked->expression_index, linked->expression_count,
        sizeof(*linked->expression_index), confit_v2_link_expression_compare);
  return CONFIT_OK;
}

static ConfitV2ExpressionType confit_v2_link_bool_type(void) {
  ConfitV2ExpressionType type;

  memset(&type, 0, sizeof(type));
  type.kind = CONFIT_V2_EXPRESSION_TYPE_BOOL;
  return type;
}

static ConfitStatus confit_v2_link_symbol_expressions(
    ConfitV2LinkedProject *linked, const ConfitV2Symbol *symbol,
    ConfitDiagnostic *diagnostic) {
  const ConfitV2ExpressionType bool_type = confit_v2_link_bool_type();
  const ConfitV2ExpressionType value_type =
      confit_v2_expression_type_from_option_type(symbol->type, symbol->id);
  ConfitStatus status;
  size_t index;

  status = confit_v2_link_append_expression(
      linked, CONFIT_V2_LINKED_EXPRESSION_COMPUTED, symbol->id, symbol->id,
      &symbol->computed, &value_type, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_link_append_expression(
      linked, CONFIT_V2_LINKED_EXPRESSION_AVAILABLE_IF, symbol->id, symbol->id,
      &symbol->available_if, &bool_type, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_link_append_expression(
      linked, CONFIT_V2_LINKED_EXPRESSION_VISIBLE_IF, symbol->id, symbol->id,
      &symbol->visible_if, &bool_type, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  for (index = 0U; index < symbol->default_count; ++index) {
    status = confit_v2_link_append_expression(
        linked, CONFIT_V2_LINKED_EXPRESSION_DEFAULT_WHEN, symbol->id, symbol->id,
        &symbol->defaults[index].when, &bool_type, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  for (index = 0U; index < symbol->suggestion_count; ++index) {
    status = confit_v2_link_append_expression(
        linked, CONFIT_V2_LINKED_EXPRESSION_SUGGESTION_WHEN, symbol->id,
        symbol->id, &symbol->suggestions[index].when, &bool_type, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_link_non_symbol_expressions(
    ConfitV2LinkedProject *linked, ConfitDiagnostic *diagnostic) {
  const ConfitV2Project *project = linked->project;
  const ConfitV2ExpressionType bool_type = confit_v2_link_bool_type();
  ConfitStatus status;
  size_t index;

  for (index = 0U; index < project->menu_count; ++index) {
    status = confit_v2_link_append_expression(
        linked, CONFIT_V2_LINKED_EXPRESSION_MENU_VISIBLE_IF,
        project->menus[index].id, 0, &project->menus[index].visible_if,
        &bool_type, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  for (index = 0U; index < project->choice_count; ++index) {
    const ConfitV2Choice *choice = &project->choices[index];
    size_t default_index;
    status = confit_v2_link_append_expression(
        linked, CONFIT_V2_LINKED_EXPRESSION_CHOICE_AVAILABLE_IF, choice->id, 0,
        &choice->available_if, &bool_type, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    status = confit_v2_link_append_expression(
        linked, CONFIT_V2_LINKED_EXPRESSION_CHOICE_VISIBLE_IF, choice->id, 0,
        &choice->visible_if, &bool_type, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    for (default_index = 0U; default_index < choice->default_count;
         ++default_index) {
      status = confit_v2_link_append_expression(
          linked, CONFIT_V2_LINKED_EXPRESSION_CHOICE_DEFAULT_WHEN, choice->id,
          0, &choice->defaults[default_index].when, &bool_type, diagnostic);
      if (status != CONFIT_OK) {
        return status;
      }
    }
  }
  for (index = 0U; index < project->constraint_count; ++index) {
    const ConfitV2Constraint *constraint = &project->constraints[index];
    status = confit_v2_link_append_expression(
        linked, CONFIT_V2_LINKED_EXPRESSION_CONSTRAINT_WHEN, constraint->id, 0,
        &constraint->when, &bool_type, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    status = confit_v2_link_append_expression(
        linked, CONFIT_V2_LINKED_EXPRESSION_CONSTRAINT_REQUIRE, constraint->id,
        0, &constraint->require, &bool_type, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

ConfitStatus confit_v2_schema_link_project(
    const ConfitV2Project *project, ConfitV2LinkedProject **out_linked,
    ConfitDiagnostic *diagnostic) {
  ConfitV2LinkedProject *linked;
  ConfitStatus status;
  size_t index;

  if (project == 0 || out_linked == 0 || project->namespace_name == 0 ||
      project->namespace_name[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                          project != 0 ? project->config_root : 0, 0, 0,
                          kInvalidLinkArgument);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_linked = 0;
  linked = (ConfitV2LinkedProject *)calloc(1U, sizeof(*linked));
  if (linked == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, project->config_root,
                          0, 0, kLinkAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  linked->project = project;
  linked->symbol_count = project->symbol_count;
  if (linked->symbol_count > 0U) {
    linked->symbols = (const ConfitV2Symbol **)calloc(linked->symbol_count,
                                                       sizeof(*linked->symbols));
    linked->bindings = (ConfitV2ExpressionBinding *)calloc(
        linked->symbol_count, sizeof(*linked->bindings));
    if (linked->symbols == 0 || linked->bindings == 0) {
      confit_v2_linked_project_free(linked);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, project->config_root,
                            0, 0, kLinkAllocationFailed);
      return CONFIT_ERR_INTERNAL;
    }
  }
  for (index = 0U; index < linked->symbol_count; ++index) {
    linked->symbols[index] = &project->symbols[index];
  }
  qsort(linked->symbols, linked->symbol_count, sizeof(*linked->symbols),
        confit_v2_link_symbol_compare);
  for (index = 0U; index < linked->symbol_count; ++index) {
    const ConfitV2Symbol *symbol = linked->symbols[index];
    if (!confit_v2_link_symbol_is_owned_by_namespace(project, symbol)) {
      confit_v2_link_source_diagnostic(&symbol->span, CONFIT_ERR_SCHEMA,
                                       kInvalidLinkedNamespace, diagnostic);
      confit_v2_linked_project_free(linked);
      return CONFIT_ERR_SCHEMA;
    }
    if (index > 0U && strcmp(linked->symbols[index - 1U]->id, symbol->id) == 0) {
      confit_v2_link_source_diagnostic(&symbol->span, CONFIT_ERR_SCHEMA,
                                       kDuplicateLinkedSymbol, diagnostic);
      confit_v2_linked_project_free(linked);
      return CONFIT_ERR_SCHEMA;
    }
    linked->bindings[index].id = symbol->id;
    linked->bindings[index].type =
        confit_v2_expression_type_from_option_type(symbol->type, symbol->id);
    if (linked->bindings[index].type.kind == CONFIT_V2_EXPRESSION_TYPE_INVALID) {
      confit_v2_link_source_diagnostic(&symbol->span, CONFIT_ERR_SCHEMA,
                                       kInvalidLinkedNamespace, diagnostic);
      confit_v2_linked_project_free(linked);
      return CONFIT_ERR_SCHEMA;
    }
  }
  for (index = 0U; index < linked->symbol_count; ++index) {
    status = confit_v2_link_symbol_expressions(linked, linked->symbols[index],
                                                diagnostic);
    if (status != CONFIT_OK) {
      confit_v2_linked_project_free(linked);
      return status;
    }
  }
  status = confit_v2_link_non_symbol_expressions(linked, diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_linked_project_free(linked);
    return status;
  }
  status = confit_v2_link_build_expression_index(linked, diagnostic);
  if (status != CONFIT_OK) {
    confit_v2_linked_project_free(linked);
    return status;
  }
  *out_linked = linked;
  return CONFIT_OK;
}

void confit_v2_linked_project_free(ConfitV2LinkedProject *linked) {
  size_t index;

  if (linked == 0) {
    return;
  }
  for (index = 0U; index < linked->expression_count; ++index) {
    free((ConfitV2LinkedReference *)linked->expressions[index].references);
    confit_v2_typed_expression_free(
        (ConfitV2TypedExpression *)linked->expressions[index].typed);
    confit_v2_expression_free(
        (ConfitV2Expression *)linked->expressions[index].expression);
  }
  free(linked->expressions);
  free(linked->expression_index);
  free(linked->bindings);
  free(linked->symbols);
  free(linked);
}

const ConfitV2Project *confit_v2_linked_project_source(
    const ConfitV2LinkedProject *linked) {
  return linked != 0 ? linked->project : 0;
}

size_t confit_v2_linked_project_symbol_count(const ConfitV2LinkedProject *linked) {
  return linked != 0 ? linked->symbol_count : 0U;
}

const ConfitV2Symbol *confit_v2_linked_project_find_symbol(
    const ConfitV2LinkedProject *linked, const char *id) {
  return confit_v2_link_find_symbol(linked, id);
}

size_t confit_v2_linked_project_expression_count(
    const ConfitV2LinkedProject *linked) {
  return linked != 0 ? linked->expression_count : 0U;
}

const ConfitV2LinkedExpression *confit_v2_linked_project_expression_at(
    const ConfitV2LinkedProject *linked, size_t index) {
  if (linked == 0 || index >= linked->expression_count) {
    return 0;
  }
  return &linked->expressions[index];
}

const ConfitV2LinkedExpression *confit_v2_linked_project_find_expression(
    const ConfitV2LinkedProject *linked, ConfitV2LinkedExpressionRole role,
    const char *owner_id, size_t occurrence) {
  size_t low;
  size_t high;

  if (linked == 0 || owner_id == 0 || linked->expression_index == 0) {
    return 0;
  }
  low = 0U;
  high = linked->expression_count;
  while (low < high) {
    const size_t middle = low + (high - low) / 2U;
    const ConfitV2LinkedExpression *expression = linked->expression_index[middle];
    const int comparison = expression->role == role
                               ? strcmp(expression->owner_id, owner_id)
                               : (expression->role < role ? -1 : 1);

    if (comparison < 0) {
      low = middle + 1U;
    } else {
      high = middle;
    }
  }
  while (low < linked->expression_count) {
    const ConfitV2LinkedExpression *expression = linked->expression_index[low];

    if (expression->role != role || strcmp(expression->owner_id, owner_id) != 0) {
      return 0;
    }
    if (occurrence == 0U) {
      return expression;
    }
    occurrence -= 1U;
    low += 1U;
  }
  return 0;
}

ConfitStatus confit_v2_linked_project_validate_write(
    const ConfitV2LinkedProject *linked, const ConfitV2WriteRequest *request,
    ConfitDiagnostic *diagnostic) {
  const ConfitV2Symbol *symbol;
  int allowed = 0;

  if (linked == 0 || request == 0 || request->option_id == 0 ||
      request->option_id[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          kInvalidLinkArgument);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  symbol = confit_v2_link_find_symbol(linked, request->option_id);
  if (symbol == 0) {
    confit_v2_link_source_diagnostic(&request->span, CONFIT_ERR_SCHEMA,
                                     kUnknownWriteOption, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  if (symbol->write_domain == CONFIT_V2_WRITE_DOMAIN_COMPUTED &&
      request->writer != CONFIT_V2_ASSIGNMENT_WRITER_COMPUTED) {
    confit_v2_link_source_diagnostic(&request->span, CONFIT_ERR_SCHEMA,
                                     kComputedExternalWrite, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  switch (request->writer) {
  case CONFIT_V2_ASSIGNMENT_WRITER_SCHEMA:
    allowed = symbol->write_domain != CONFIT_V2_WRITE_DOMAIN_COMPUTED;
    break;
  case CONFIT_V2_ASSIGNMENT_WRITER_PROFILE:
    allowed = symbol->write_domain == CONFIT_V2_WRITE_DOMAIN_PROFILE;
    break;
  case CONFIT_V2_ASSIGNMENT_WRITER_TARGET:
    allowed = symbol->write_domain == CONFIT_V2_WRITE_DOMAIN_TARGET;
    break;
  case CONFIT_V2_ASSIGNMENT_WRITER_USER:
    allowed = (symbol->write_domain == CONFIT_V2_WRITE_DOMAIN_PROFILE ||
               symbol->write_domain == CONFIT_V2_WRITE_DOMAIN_TARGET) &&
              symbol->user_override;
    break;
  case CONFIT_V2_ASSIGNMENT_WRITER_COMPUTED:
    allowed = symbol->write_domain == CONFIT_V2_WRITE_DOMAIN_COMPUTED;
    break;
  default:
    break;
  }
  if (!allowed) {
    confit_v2_link_source_diagnostic(&request->span, CONFIT_ERR_SCHEMA,
                                     kWriteDomainViolation, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}
