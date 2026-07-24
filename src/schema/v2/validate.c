#include "confit/constraint_v2.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../constraint/v2/compiled_internal.h"

static const char kInvalidCompileArgument[] =
    "invalid schema v2 structure compiler argument";
static const char kAllocationFailed[] =
    "failed to allocate schema v2 structure data";
static const char kMissingMenuParent[] = "schema v2 menu parent is missing";
static const char kMenuCycle[] = "schema v2 menu tree cycle";
static const char kDuplicateMenuOrder[] =
    "schema v2 menu siblings have duplicate order";
static const char kMissingSymbolMenu[] =
    "schema v2 option references missing menu";
static const char kMissingMenuReferenceOption[] =
    "schema v2 menu reference targets missing option";
static const char kWritableMenuReference[] =
    "schema v2 menu reference must be read_only";
static const char kDuplicateMenuReference[] =
    "schema v2 menu has duplicate option reference";

void confit_v2_structure_diagnostic(const ConfitV2SourceSpan *span,
                                    ConfitStatus status, const char *message,
                                    ConfitDiagnostic *diagnostic) {
  confit_diagnostic_set(diagnostic, status, span != 0 ? span->path : 0,
                        span != 0 ? span->line : 0U,
                        span != 0 ? span->column : 0U, message);
}

const ConfitV2LinkedExpression *confit_v2_structure_find_expression(
    const ConfitV2LinkedProject *linked, ConfitV2LinkedExpressionRole role,
    const char *owner_id, size_t occurrence) {
  size_t index;

  if (linked == 0 || owner_id == 0) {
    return 0;
  }
  for (index = 0U; index < confit_v2_linked_project_expression_count(linked);
       ++index) {
    const ConfitV2LinkedExpression *expression =
        confit_v2_linked_project_expression_at(linked, index);
    if (expression != 0 && expression->role == role &&
        strcmp(expression->owner_id, owner_id) == 0) {
      if (occurrence == 0U) {
        return expression;
      }
      occurrence -= 1U;
    }
  }
  return 0;
}

static int confit_v2_compiled_menu_compare(const void *left, const void *right) {
  const ConfitV2CompiledMenu *left_menu = (const ConfitV2CompiledMenu *)left;
  const ConfitV2CompiledMenu *right_menu =
      (const ConfitV2CompiledMenu *)right;

  return strcmp(left_menu->source->id, right_menu->source->id);
}

static void confit_v2_menu_cycle_diagnostic(
    const ConfitV2CompiledMenu *const *path, size_t begin, size_t end,
    ConfitDiagnostic *diagnostic) {
  static _Thread_local char message[1024];
  size_t offset = 0U;
  size_t index;

  (void)snprintf(message, sizeof(message), "%s: ", kMenuCycle);
  offset = strlen(message);
  for (index = begin; index < end; ++index) {
    const char *id = path[index]->source->id;
    const int written = snprintf(message + offset, sizeof(message) - offset,
                                 "%s%s", index == begin ? "" : " -> ", id);
    if (written < 0 || (size_t)written >= sizeof(message) - offset) {
      break;
    }
    offset += (size_t)written;
  }
  if (offset < sizeof(message) - 1U) {
    (void)snprintf(message + offset, sizeof(message) - offset, " -> %s",
                   path[begin]->source->id);
  }
  confit_v2_structure_diagnostic(&path[begin]->source->span, CONFIT_ERR_SCHEMA,
                                 message, diagnostic);
}

const ConfitV2CompiledMenu *confit_v2_compiled_structure_find_menu(
    const ConfitV2CompiledStructure *compiled, const char *id) {
  size_t low = 0U;
  size_t high;

  if (compiled == 0 || id == 0) {
    return 0;
  }
  high = compiled->menu_count;
  while (low < high) {
    const size_t middle = low + (high - low) / 2U;
    const int comparison = strcmp(id, compiled->menus[middle].source->id);
    if (comparison == 0) {
      return &compiled->menus[middle];
    }
    if (comparison < 0) {
      high = middle;
    } else {
      low = middle + 1U;
    }
  }
  return 0;
}

static ConfitStatus confit_v2_compile_menus(ConfitV2CompiledStructure *compiled,
                                             ConfitDiagnostic *diagnostic) {
  const ConfitV2Project *project =
      confit_v2_linked_project_source(compiled->linked);
  size_t index;

  compiled->menu_count = project->menu_count;
  if (compiled->menu_count > 0U) {
    compiled->menus = (ConfitV2CompiledMenu *)calloc(
        compiled->menu_count, sizeof(*compiled->menus));
    if (compiled->menus == 0) {
      confit_v2_structure_diagnostic(&project->span, CONFIT_ERR_INTERNAL,
                                     kAllocationFailed, diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
  }
  for (index = 0U; index < compiled->menu_count; ++index) {
    compiled->menus[index].source = &project->menus[index];
    compiled->menus[index].visible_if = confit_v2_structure_find_expression(
        compiled->linked, CONFIT_V2_LINKED_EXPRESSION_MENU_VISIBLE_IF,
        project->menus[index].id, 0U);
  }
  if (compiled->menu_count > 1U) {
    qsort(compiled->menus, compiled->menu_count, sizeof(*compiled->menus),
          confit_v2_compiled_menu_compare);
  }
  for (index = 0U; index < compiled->menu_count; ++index) {
    const ConfitV2MenuNode *menu = compiled->menus[index].source;
    if (menu->visible_if.text != 0 && compiled->menus[index].visible_if == 0) {
      confit_v2_structure_diagnostic(&menu->visible_if.span, CONFIT_ERR_INTERNAL,
                                     kAllocationFailed, diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
    if (menu->parent != 0) {
      const ConfitV2CompiledMenu *parent =
          confit_v2_compiled_structure_find_menu(compiled, menu->parent);
      if (parent == 0) {
        confit_v2_structure_diagnostic(&menu->span, CONFIT_ERR_SCHEMA,
                                       kMissingMenuParent, diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
      compiled->menus[index].parent = parent;
    }
  }
  for (index = 0U; index < compiled->menu_count; ++index) {
    const ConfitV2CompiledMenu *cursor = &compiled->menus[index];
    const ConfitV2CompiledMenu **path = (const ConfitV2CompiledMenu **)calloc(
        compiled->menu_count, sizeof(*path));
    size_t depth = 0U;
    if (path == 0) {
      confit_v2_structure_diagnostic(&compiled->menus[index].source->span,
                                     CONFIT_ERR_INTERNAL, kAllocationFailed,
                                     diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
    while (cursor != 0) {
      size_t previous;
      for (previous = 0U; previous < depth; ++previous) {
        if (path[previous] == cursor) {
          confit_v2_menu_cycle_diagnostic(path, previous, depth, diagnostic);
          free(path);
          return CONFIT_ERR_SCHEMA;
        }
      }
      if (depth == compiled->menu_count) {
        free(path);
        confit_v2_structure_diagnostic(&compiled->menus[index].source->span,
                                       CONFIT_ERR_INTERNAL, kAllocationFailed,
                                       diagnostic);
        return CONFIT_ERR_INTERNAL;
      }
      path[depth] = cursor;
      depth += 1U;
      cursor = cursor->parent;
    }
    free(path);
  }
  for (index = 0U; index < compiled->menu_count; ++index) {
    size_t other;
    for (other = index + 1U; other < compiled->menu_count; ++other) {
      if (compiled->menus[index].parent == compiled->menus[other].parent &&
          compiled->menus[index].source->order ==
              compiled->menus[other].source->order) {
        confit_v2_structure_diagnostic(&compiled->menus[other].source->span,
                                       CONFIT_ERR_SCHEMA, kDuplicateMenuOrder,
                                       diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_compile_menu_references(
    ConfitV2CompiledStructure *compiled, ConfitDiagnostic *diagnostic) {
  const ConfitV2Project *project =
      confit_v2_linked_project_source(compiled->linked);
  size_t total = 0U;
  size_t menu_index;
  size_t output = 0U;

  for (menu_index = 0U; menu_index < project->menu_count; ++menu_index) {
    if (project->menus[menu_index].reference_count > SIZE_MAX - total) {
      confit_v2_structure_diagnostic(&project->menus[menu_index].span,
                                     CONFIT_ERR_INTERNAL, kAllocationFailed,
                                     diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
    total += project->menus[menu_index].reference_count;
  }
  if (total > 0U) {
    compiled->menu_references = (ConfitV2CompiledMenuReference *)calloc(
        total, sizeof(*compiled->menu_references));
    if (compiled->menu_references == 0) {
      confit_v2_structure_diagnostic(&project->span, CONFIT_ERR_INTERNAL,
                                     kAllocationFailed, diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
  }
  for (menu_index = 0U; menu_index < project->menu_count; ++menu_index) {
    const ConfitV2MenuNode *menu = &project->menus[menu_index];
    const ConfitV2CompiledMenu *compiled_menu =
        confit_v2_compiled_structure_find_menu(compiled, menu->id);
    size_t reference_index;
    for (reference_index = 0U; reference_index < menu->reference_count;
         ++reference_index) {
      const ConfitV2MenuReference *reference = &menu->references[reference_index];
      size_t previous;
      const ConfitV2Symbol *symbol;

      if (!reference->read_only) {
        confit_v2_structure_diagnostic(&reference->span, CONFIT_ERR_SCHEMA,
                                       kWritableMenuReference, diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
      symbol = confit_v2_linked_project_find_symbol(compiled->linked,
                                                      reference->option_id);
      if (symbol == 0) {
        confit_v2_structure_diagnostic(&reference->span, CONFIT_ERR_SCHEMA,
                                       kMissingMenuReferenceOption, diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
      for (previous = 0U; previous < reference_index; ++previous) {
        if (strcmp(menu->references[previous].option_id, reference->option_id) ==
            0) {
          confit_v2_structure_diagnostic(&reference->span, CONFIT_ERR_SCHEMA,
                                         kDuplicateMenuReference, diagnostic);
          return CONFIT_ERR_SCHEMA;
        }
      }
      compiled->menu_references[output].source = reference;
      compiled->menu_references[output].menu = compiled_menu;
      compiled->menu_references[output].symbol = symbol;
      output += 1U;
    }
  }
  compiled->menu_reference_count = output;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_validate_symbol_menus(
    ConfitV2CompiledStructure *compiled, ConfitDiagnostic *diagnostic) {
  const ConfitV2Project *project =
      confit_v2_linked_project_source(compiled->linked);
  size_t index;

  for (index = 0U; index < project->symbol_count; ++index) {
    const ConfitV2Symbol *symbol = &project->symbols[index];
    if (symbol->menu != 0 &&
        confit_v2_compiled_structure_find_menu(compiled, symbol->menu) == 0) {
      confit_v2_structure_diagnostic(&symbol->span, CONFIT_ERR_SCHEMA,
                                     kMissingSymbolMenu, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
  }
  return CONFIT_OK;
}

ConfitStatus confit_v2_compile_structure(
    const ConfitV2LinkedProject *linked, ConfitV2CompiledStructure **out_compiled,
    ConfitDiagnostic *diagnostic) {
  ConfitV2CompiledStructure *compiled;
  ConfitStatus status;

  if (linked == 0 || out_compiled == 0 ||
      confit_v2_linked_project_source(linked) == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          kInvalidCompileArgument);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_compiled = 0;
  compiled = (ConfitV2CompiledStructure *)calloc(1U, sizeof(*compiled));
  if (compiled == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  compiled->linked = linked;
  status = confit_v2_compile_menus(compiled, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_validate_symbol_menus(compiled, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_compile_menu_references(compiled, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_structure_compile_choices_and_constraints(compiled,
                                                                  diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_structure_build_graphs(compiled, diagnostic);
  }
  if (status != CONFIT_OK) {
    confit_v2_compiled_structure_free(compiled);
    return status;
  }
  *out_compiled = compiled;
  return CONFIT_OK;
}

void confit_v2_compiled_structure_free(ConfitV2CompiledStructure *compiled) {
  size_t index;

  if (compiled == 0) {
    return;
  }
  for (index = 0U; index < compiled->choice_count; ++index) {
    free(compiled->choices[index].members);
    free(compiled->choices[index].default_when);
  }
  confit_v2_structure_graphs_clear(compiled);
  free(compiled->constraints);
  free(compiled->choices);
  free(compiled->menu_references);
  free(compiled->menus);
  free(compiled);
}

const ConfitV2LinkedProject *confit_v2_compiled_structure_source(
    const ConfitV2CompiledStructure *compiled) {
  return compiled != 0 ? compiled->linked : 0;
}

size_t confit_v2_compiled_structure_menu_count(
    const ConfitV2CompiledStructure *compiled) {
  return compiled != 0 ? compiled->menu_count : 0U;
}

const ConfitV2CompiledMenu *confit_v2_compiled_structure_menu_at(
    const ConfitV2CompiledStructure *compiled, size_t index) {
  if (compiled == 0 || index >= compiled->menu_count) {
    return 0;
  }
  return &compiled->menus[index];
}

size_t confit_v2_compiled_structure_menu_reference_count(
    const ConfitV2CompiledStructure *compiled) {
  return compiled != 0 ? compiled->menu_reference_count : 0U;
}

const ConfitV2CompiledMenuReference *
confit_v2_compiled_structure_menu_reference_at(
    const ConfitV2CompiledStructure *compiled, size_t index) {
  if (compiled == 0 || index >= compiled->menu_reference_count) {
    return 0;
  }
  return &compiled->menu_references[index];
}

size_t confit_v2_compiled_structure_choice_count(
    const ConfitV2CompiledStructure *compiled) {
  return compiled != 0 ? compiled->choice_count : 0U;
}

const ConfitV2CompiledChoice *confit_v2_compiled_structure_choice_at(
    const ConfitV2CompiledStructure *compiled, size_t index) {
  if (compiled == 0 || index >= compiled->choice_count) {
    return 0;
  }
  return &compiled->choices[index];
}

size_t confit_v2_compiled_structure_constraint_count(
    const ConfitV2CompiledStructure *compiled) {
  return compiled != 0 ? compiled->constraint_count : 0U;
}

const ConfitV2CompiledConstraint *confit_v2_compiled_structure_constraint_at(
    const ConfitV2CompiledStructure *compiled, size_t index) {
  if (compiled == 0 || index >= compiled->constraint_count) {
    return 0;
  }
  return &compiled->constraints[index];
}
