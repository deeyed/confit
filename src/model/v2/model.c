#include "confit/schema_v2.h"

#include <stdlib.h>
#include <string.h>

#include "model_internal.h"

static void *confit_v2_default_allocate(void *context, size_t size) {
  (void)context;
  return malloc(size);
}

static void *confit_v2_default_reallocate(void *context, void *allocation,
                                          size_t size) {
  (void)context;
  return realloc(allocation, size);
}

static void confit_v2_default_deallocate(void *context, void *allocation) {
  (void)context;
  free(allocation);
}

static const ConfitV2TypeDescriptor kTypeDescriptors[] = {
    {CONFIT_V2_OPTION_TYPE_BOOL, CONFIT_V2_VALUE_BOOL, 0, 0},
    {CONFIT_V2_OPTION_TYPE_TRISTATE, CONFIT_V2_VALUE_TRISTATE, 0, 0},
    {CONFIT_V2_OPTION_TYPE_INT, CONFIT_V2_VALUE_INT, 0, 0},
    {CONFIT_V2_OPTION_TYPE_UINT, CONFIT_V2_VALUE_UINT, 0, 0},
    {CONFIT_V2_OPTION_TYPE_HEX, CONFIT_V2_VALUE_UINT, 0, 0},
    {CONFIT_V2_OPTION_TYPE_FLOAT, CONFIT_V2_VALUE_FLOAT, 0, 0},
    {CONFIT_V2_OPTION_TYPE_STRING, CONFIT_V2_VALUE_STRING, 0, 0},
    {CONFIT_V2_OPTION_TYPE_ENUM, CONFIT_V2_VALUE_STRING, 0, 1},
    {CONFIT_V2_OPTION_TYPE_PATH, CONFIT_V2_VALUE_STRING, 0, 0},
    {CONFIT_V2_OPTION_TYPE_STRING_LIST, CONFIT_V2_VALUE_STRING_LIST, 1, 0},
    {CONFIT_V2_OPTION_TYPE_PATH_LIST, CONFIT_V2_VALUE_STRING_LIST, 1, 0},
    {CONFIT_V2_OPTION_TYPE_ENUM_SET, CONFIT_V2_VALUE_STRING_LIST, 1, 1},
};

int confit_v2_allocator_is_valid(const ConfitV2Allocator *allocator) {
  return allocator != 0 && allocator->allocate != 0 &&
         allocator->reallocate != 0 && allocator->deallocate != 0;
}

void *confit_v2_allocate(const ConfitV2Allocator *allocator, size_t size) {
  if (!confit_v2_allocator_is_valid(allocator) || size == 0U) {
    return 0;
  }
  return allocator->allocate(allocator->context, size);
}

void *confit_v2_reallocate(const ConfitV2Allocator *allocator, void *allocation,
                           size_t size) {
  if (!confit_v2_allocator_is_valid(allocator) || size == 0U) {
    return 0;
  }
  return allocator->reallocate(allocator->context, allocation, size);
}

void confit_v2_deallocate(const ConfitV2Allocator *allocator,
                          void *allocation) {
  if (allocation != 0 && confit_v2_allocator_is_valid(allocator)) {
    allocator->deallocate(allocator->context, allocation);
  }
}

char *confit_v2_strdup(const ConfitV2Allocator *allocator, const char *text) {
  char *copy;
  size_t size;

  if (text == 0) {
    return 0;
  }
  size = strlen(text);
  copy = (char *)confit_v2_allocate(allocator, size + 1U);
  if (copy == 0) {
    return 0;
  }
  memcpy(copy, text, size + 1U);
  return copy;
}

const ConfitV2TypeDescriptor *
confit_v2_type_descriptor(ConfitV2OptionType option_type) {
  size_t index;

  for (index = 0U; index < sizeof(kTypeDescriptors) / sizeof(kTypeDescriptors[0]);
       ++index) {
    if (kTypeDescriptors[index].option_type == option_type) {
      return &kTypeDescriptors[index];
    }
  }
  return 0;
}

void confit_v2_source_span_clear(const ConfitV2Allocator *allocator,
                                 ConfitV2SourceSpan *span) {
  if (span == 0) {
    return;
  }
  confit_v2_deallocate(allocator, span->path);
  memset(span, 0, sizeof(*span));
}

void confit_v2_string_list_clear(const ConfitV2Allocator *allocator,
                                 ConfitV2StringList *list) {
  size_t index;

  if (list == 0) {
    return;
  }
  for (index = 0U; index < list->count; ++index) {
    confit_v2_deallocate(allocator, list->items[index]);
  }
  confit_v2_deallocate(allocator, list->items);
  memset(list, 0, sizeof(*list));
}

void confit_v2_value_clear(const ConfitV2Allocator *allocator,
                           ConfitV2Value *value) {
  if (value == 0) {
    return;
  }
  if (value->kind == CONFIT_V2_VALUE_STRING) {
    confit_v2_deallocate(allocator, value->as.string_value);
  } else if (value->kind == CONFIT_V2_VALUE_STRING_LIST) {
    confit_v2_string_list_clear(allocator, &value->as.string_list);
  }
  memset(value, 0, sizeof(*value));
}

static void confit_v2_expression_clear(const ConfitV2Allocator *allocator,
                                       ConfitV2ExpressionText *expression) {
  if (expression == 0) {
    return;
  }
  confit_v2_deallocate(allocator, expression->text);
  confit_v2_source_span_clear(allocator, &expression->span);
  memset(expression, 0, sizeof(*expression));
}

static void confit_v2_assignment_clear(const ConfitV2Allocator *allocator,
                                       ConfitV2Assignment *assignment) {
  if (assignment == 0) {
    return;
  }
  confit_v2_value_clear(allocator, &assignment->value);
  confit_v2_source_span_clear(allocator, &assignment->span);
  memset(assignment, 0, sizeof(*assignment));
}

static void confit_v2_symbol_clear(const ConfitV2Allocator *allocator,
                                   ConfitV2Symbol *symbol) {
  size_t index;

  if (symbol == 0) {
    return;
  }
  confit_v2_deallocate(allocator, symbol->id);
  confit_v2_assignment_clear(allocator, &symbol->default_value);
  confit_v2_value_clear(allocator, &symbol->range.min_value);
  confit_v2_value_clear(allocator, &symbol->range.max_value);
  confit_v2_source_span_clear(allocator, &symbol->range.span);
  confit_v2_string_list_clear(allocator, &symbol->values);
  confit_v2_deallocate(allocator, symbol->prompt);
  confit_v2_deallocate(allocator, symbol->help);
  confit_v2_deallocate(allocator, symbol->menu);
  confit_v2_string_list_clear(allocator, &symbol->tags);
  confit_v2_deallocate(allocator, symbol->owner);
  confit_v2_deallocate(allocator, symbol->since);
  confit_v2_expression_clear(allocator, &symbol->computed);
  confit_v2_expression_clear(allocator, &symbol->available_if);
  confit_v2_expression_clear(allocator, &symbol->visible_if);
  for (index = 0U; index < symbol->default_count; ++index) {
    confit_v2_expression_clear(allocator, &symbol->defaults[index].when);
    confit_v2_assignment_clear(allocator, &symbol->defaults[index].assignment);
    confit_v2_source_span_clear(allocator, &symbol->defaults[index].span);
  }
  confit_v2_deallocate(allocator, symbol->defaults);
  for (index = 0U; index < symbol->suggestion_count; ++index) {
    confit_v2_expression_clear(allocator, &symbol->suggestions[index].when);
    confit_v2_assignment_clear(allocator,
                               &symbol->suggestions[index].assignment);
    confit_v2_deallocate(allocator, symbol->suggestions[index].message);
    confit_v2_source_span_clear(allocator, &symbol->suggestions[index].span);
  }
  confit_v2_deallocate(allocator, symbol->suggestions);
  confit_v2_source_span_clear(allocator, &symbol->span);
  memset(symbol, 0, sizeof(*symbol));
}

static void confit_v2_menu_clear(const ConfitV2Allocator *allocator,
                                 ConfitV2MenuNode *menu) {
  size_t index;

  if (menu == 0) {
    return;
  }
  confit_v2_deallocate(allocator, menu->id);
  confit_v2_deallocate(allocator, menu->prompt);
  confit_v2_deallocate(allocator, menu->parent);
  confit_v2_expression_clear(allocator, &menu->visible_if);
  for (index = 0U; index < menu->reference_count; ++index) {
    confit_v2_deallocate(allocator, menu->references[index].option_id);
    confit_v2_source_span_clear(allocator, &menu->references[index].span);
  }
  confit_v2_deallocate(allocator, menu->references);
  confit_v2_source_span_clear(allocator, &menu->span);
  memset(menu, 0, sizeof(*menu));
}

static void confit_v2_choice_clear(const ConfitV2Allocator *allocator,
                                   ConfitV2Choice *choice) {
  size_t index;

  if (choice == 0) {
    return;
  }
  confit_v2_deallocate(allocator, choice->id);
  confit_v2_string_list_clear(allocator, &choice->members);
  confit_v2_expression_clear(allocator, &choice->available_if);
  confit_v2_expression_clear(allocator, &choice->visible_if);
  for (index = 0U; index < choice->default_count; ++index) {
    confit_v2_expression_clear(allocator, &choice->defaults[index].when);
    confit_v2_deallocate(allocator, choice->defaults[index].member);
    confit_v2_source_span_clear(allocator, &choice->defaults[index].span);
  }
  confit_v2_deallocate(allocator, choice->defaults);
  confit_v2_source_span_clear(allocator, &choice->span);
  memset(choice, 0, sizeof(*choice));
}

static void confit_v2_constraint_clear(const ConfitV2Allocator *allocator,
                                       ConfitV2Constraint *constraint) {
  if (constraint == 0) {
    return;
  }
  confit_v2_deallocate(allocator, constraint->id);
  confit_v2_expression_clear(allocator, &constraint->when);
  confit_v2_expression_clear(allocator, &constraint->require);
  confit_v2_deallocate(allocator, constraint->message);
  confit_v2_source_span_clear(allocator, &constraint->span);
  memset(constraint, 0, sizeof(*constraint));
}

void confit_v2_project_free(ConfitV2Project *project) {
  size_t index;
  const ConfitV2Allocator *allocator;

  if (project == 0) {
    return;
  }
  allocator = &project->allocator;
  confit_v2_deallocate(allocator, project->config_root);
  confit_v2_deallocate(allocator, project->name);
  confit_v2_deallocate(allocator, project->namespace_name);
  confit_v2_deallocate(allocator, project->version);
  for (index = 0U; index < project->import_count; ++index) {
    confit_v2_deallocate(allocator, project->imports[index].path);
    confit_v2_deallocate(allocator, project->imports[index].canonical_path);
    confit_v2_source_span_clear(allocator, &project->imports[index].span);
  }
  confit_v2_deallocate(allocator, project->imports);
  confit_v2_string_list_clear(allocator, &project->profile_dirs);
  confit_v2_string_list_clear(allocator, &project->target_dirs);
  confit_v2_string_list_clear(allocator, &project->selection_dirs);
  for (index = 0U; index < project->symbol_count; ++index) {
    confit_v2_symbol_clear(allocator, &project->symbols[index]);
  }
  confit_v2_deallocate(allocator, project->symbols);
  for (index = 0U; index < project->menu_count; ++index) {
    confit_v2_menu_clear(allocator, &project->menus[index]);
  }
  confit_v2_deallocate(allocator, project->menus);
  for (index = 0U; index < project->choice_count; ++index) {
    confit_v2_choice_clear(allocator, &project->choices[index]);
  }
  confit_v2_deallocate(allocator, project->choices);
  for (index = 0U; index < project->constraint_count; ++index) {
    confit_v2_constraint_clear(allocator, &project->constraints[index]);
  }
  confit_v2_deallocate(allocator, project->constraints);
  confit_v2_source_span_clear(allocator, &project->span);
  confit_v2_deallocate(allocator, project);
}

const ConfitV2Allocator *confit_v2_default_allocator(void) {
  static const ConfitV2Allocator allocator = {
      0, confit_v2_default_allocate, confit_v2_default_reallocate,
      confit_v2_default_deallocate};
  return &allocator;
}
