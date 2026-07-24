#include "compiled_internal.h"

#include <stdlib.h>
#include <string.h>

static const char kAllocationFailed[] =
    "failed to allocate schema v2 structure data";
static const char kMissingChoiceMember[] =
    "schema v2 choice member references missing option";
static const char kNestedChoice[] =
    "schema v2 choice member cannot reference another choice";
static const char kChoiceMemberType[] =
    "schema v2 choice member type does not match declaration";
static const char kChoiceMemberDomain[] =
    "schema v2 choice members have different write domains";
static const char kChoiceCardinality[] =
    "schema v2 choice has impossible cardinality";
static const char kChoiceDefaultMember[] =
    "schema v2 choice default references non-member option";
static const char kChoiceDefaultTie[] =
    "schema v2 choice has ambiguous equal-priority default";
static const char kConstraintMessage[] =
    "schema v2 constraint message must not be empty";
static const char kMissingLinkedExpression[] =
    "schema v2 structure is missing linked expression";

static int confit_v2_compiled_choice_compare(const void *left, const void *right) {
  const ConfitV2CompiledChoice *left_choice =
      (const ConfitV2CompiledChoice *)left;
  const ConfitV2CompiledChoice *right_choice =
      (const ConfitV2CompiledChoice *)right;

  return strcmp(left_choice->source->id, right_choice->source->id);
}

static int confit_v2_compiled_constraint_compare(const void *left,
                                                 const void *right) {
  const ConfitV2CompiledConstraint *left_constraint =
      (const ConfitV2CompiledConstraint *)left;
  const ConfitV2CompiledConstraint *right_constraint =
      (const ConfitV2CompiledConstraint *)right;

  return strcmp(left_constraint->source->id, right_constraint->source->id);
}

static int confit_v2_source_has_choice(const ConfitV2Project *project,
                                       const char *id) {
  size_t index;

  for (index = 0U; index < project->choice_count; ++index) {
    if (strcmp(project->choices[index].id, id) == 0) {
      return 1;
    }
  }
  return 0;
}

static int confit_v2_choice_has_member(const ConfitV2Choice *choice,
                                       const char *id) {
  size_t index;

  for (index = 0U; index < choice->members.count; ++index) {
    if (strcmp(choice->members.items[index], id) == 0) {
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_v2_compile_one_choice(
    ConfitV2CompiledStructure *compiled, ConfitV2CompiledChoice *output,
    ConfitDiagnostic *diagnostic) {
  const ConfitV2Choice *choice = output->source;
  const ConfitV2Project *project =
      confit_v2_linked_project_source(compiled->linked);
  size_t index;

  if (choice->members.count == 0U ||
      (choice->cardinality != CONFIT_V2_CHOICE_CARDINALITY_EXACTLY_ONE &&
       choice->cardinality != CONFIT_V2_CHOICE_CARDINALITY_ZERO_OR_ONE &&
       choice->cardinality != CONFIT_V2_CHOICE_CARDINALITY_ONE_OR_MORE)) {
    confit_v2_structure_diagnostic(&choice->span, CONFIT_ERR_SCHEMA,
                                   kChoiceCardinality, diagnostic);
    return CONFIT_ERR_SCHEMA;
  }
  output->member_count = choice->members.count;
  output->members = (const ConfitV2Symbol **)calloc(
      output->member_count, sizeof(*output->members));
  if (output->members == 0) {
    confit_v2_structure_diagnostic(&choice->span, CONFIT_ERR_INTERNAL,
                                   kAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < output->member_count; ++index) {
    const ConfitV2Symbol *member = confit_v2_linked_project_find_symbol(
        compiled->linked, choice->members.items[index]);
    if (member == 0) {
      confit_v2_structure_diagnostic(
          &choice->span, CONFIT_ERR_SCHEMA,
          confit_v2_source_has_choice(project, choice->members.items[index])
              ? kNestedChoice
              : kMissingChoiceMember,
          diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    if (member->type != choice->member_type) {
      confit_v2_structure_diagnostic(&choice->span, CONFIT_ERR_SCHEMA,
                                     kChoiceMemberType, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    if (index > 0U &&
        member->write_domain != output->members[0]->write_domain) {
      confit_v2_structure_diagnostic(&choice->span, CONFIT_ERR_SCHEMA,
                                     kChoiceMemberDomain, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    output->members[index] = member;
  }
  output->available_if = confit_v2_structure_find_expression(
      compiled->linked, CONFIT_V2_LINKED_EXPRESSION_CHOICE_AVAILABLE_IF,
      choice->id, 0U);
  output->visible_if = confit_v2_structure_find_expression(
      compiled->linked, CONFIT_V2_LINKED_EXPRESSION_CHOICE_VISIBLE_IF,
      choice->id, 0U);
  if ((choice->available_if.text != 0 && output->available_if == 0) ||
      (choice->visible_if.text != 0 && output->visible_if == 0)) {
    confit_v2_structure_diagnostic(&choice->span, CONFIT_ERR_INTERNAL,
                                   kMissingLinkedExpression, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  output->default_count = choice->default_count;
  if (output->default_count > 0U) {
    output->default_when = (const ConfitV2LinkedExpression **)calloc(
        output->default_count, sizeof(*output->default_when));
    if (output->default_when == 0) {
      confit_v2_structure_diagnostic(&choice->span, CONFIT_ERR_INTERNAL,
                                     kAllocationFailed, diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
  }
  for (index = 0U; index < output->default_count; ++index) {
    size_t other;
    if (!confit_v2_choice_has_member(choice, choice->defaults[index].member)) {
      confit_v2_structure_diagnostic(&choice->defaults[index].span,
                                     CONFIT_ERR_SCHEMA, kChoiceDefaultMember,
                                     diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    output->default_when[index] = confit_v2_structure_find_expression(
            compiled->linked, CONFIT_V2_LINKED_EXPRESSION_CHOICE_DEFAULT_WHEN,
            choice->id, index);
    if (output->default_when[index] == 0) {
      confit_v2_structure_diagnostic(&choice->defaults[index].span,
                                     CONFIT_ERR_INTERNAL,
                                     kMissingLinkedExpression, diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
    for (other = 0U; other < index; ++other) {
      if (choice->defaults[other].priority == choice->defaults[index].priority &&
          strcmp(choice->defaults[other].member,
                 choice->defaults[index].member) != 0 &&
          strcmp(choice->defaults[other].when.text,
                 choice->defaults[index].when.text) == 0) {
        confit_v2_structure_diagnostic(&choice->defaults[index].span,
                                       CONFIT_ERR_SCHEMA, kChoiceDefaultTie,
                                       diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_compile_choices(ConfitV2CompiledStructure *compiled,
                                               ConfitDiagnostic *diagnostic) {
  const ConfitV2Project *project =
      confit_v2_linked_project_source(compiled->linked);
  size_t index;

  compiled->choice_count = project->choice_count;
  if (compiled->choice_count > 0U) {
    compiled->choices = (ConfitV2CompiledChoice *)calloc(
        compiled->choice_count, sizeof(*compiled->choices));
    if (compiled->choices == 0) {
      confit_v2_structure_diagnostic(&project->span, CONFIT_ERR_INTERNAL,
                                     kAllocationFailed, diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
  }
  for (index = 0U; index < compiled->choice_count; ++index) {
    compiled->choices[index].source = &project->choices[index];
  }
  if (compiled->choice_count > 1U) {
    qsort(compiled->choices, compiled->choice_count, sizeof(*compiled->choices),
          confit_v2_compiled_choice_compare);
  }
  for (index = 0U; index < compiled->choice_count; ++index) {
    ConfitStatus status =
        confit_v2_compile_one_choice(compiled, &compiled->choices[index],
                                     diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_compile_constraints(
    ConfitV2CompiledStructure *compiled, ConfitDiagnostic *diagnostic) {
  const ConfitV2Project *project =
      confit_v2_linked_project_source(compiled->linked);
  size_t index;

  compiled->constraint_count = project->constraint_count;
  if (compiled->constraint_count > 0U) {
    compiled->constraints = (ConfitV2CompiledConstraint *)calloc(
        compiled->constraint_count, sizeof(*compiled->constraints));
    if (compiled->constraints == 0) {
      confit_v2_structure_diagnostic(&project->span, CONFIT_ERR_INTERNAL,
                                     kAllocationFailed, diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
  }
  for (index = 0U; index < compiled->constraint_count; ++index) {
    const ConfitV2Constraint *source = &project->constraints[index];
    compiled->constraints[index].source = source;
    if (source->message == 0 || source->message[0] == '\0') {
      confit_v2_structure_diagnostic(&source->span, CONFIT_ERR_SCHEMA,
                                     kConstraintMessage, diagnostic);
      return CONFIT_ERR_SCHEMA;
    }
    compiled->constraints[index].when = confit_v2_structure_find_expression(
        compiled->linked, CONFIT_V2_LINKED_EXPRESSION_CONSTRAINT_WHEN,
        source->id, 0U);
    compiled->constraints[index].require = confit_v2_structure_find_expression(
        compiled->linked, CONFIT_V2_LINKED_EXPRESSION_CONSTRAINT_REQUIRE,
        source->id, 0U);
    if (compiled->constraints[index].when == 0 ||
        compiled->constraints[index].require == 0) {
      confit_v2_structure_diagnostic(&source->span, CONFIT_ERR_INTERNAL,
                                     kMissingLinkedExpression, diagnostic);
      return CONFIT_ERR_INTERNAL;
    }
  }
  if (compiled->constraint_count > 1U) {
    qsort(compiled->constraints, compiled->constraint_count,
          sizeof(*compiled->constraints), confit_v2_compiled_constraint_compare);
  }
  return CONFIT_OK;
}

ConfitStatus confit_v2_structure_compile_choices_and_constraints(
    ConfitV2CompiledStructure *compiled, ConfitDiagnostic *diagnostic) {
  ConfitStatus status = confit_v2_compile_choices(compiled, diagnostic);

  if (status != CONFIT_OK) {
    return status;
  }
  return confit_v2_compile_constraints(compiled, diagnostic);
}
