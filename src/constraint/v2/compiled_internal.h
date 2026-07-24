#ifndef CONFIT_CONSTRAINT_V2_COMPILED_INTERNAL_H
#define CONFIT_CONSTRAINT_V2_COMPILED_INTERNAL_H

#include "confit/constraint_v2.h"

struct ConfitV2CompiledStructure {
  const ConfitV2LinkedProject *linked;
  ConfitV2CompiledMenu *menus;
  size_t menu_count;
  ConfitV2CompiledMenuReference *menu_references;
  size_t menu_reference_count;
  ConfitV2CompiledChoice *choices;
  size_t choice_count;
  ConfitV2CompiledConstraint *constraints;
  size_t constraint_count;
  ConfitV2CompiledGraph graphs[4];
};

void confit_v2_structure_diagnostic(const ConfitV2SourceSpan *span,
                                    ConfitStatus status, const char *message,
                                    ConfitDiagnostic *diagnostic);

const ConfitV2LinkedExpression *confit_v2_structure_find_expression(
    const ConfitV2LinkedProject *linked, ConfitV2LinkedExpressionRole role,
    const char *owner_id, size_t occurrence);

ConfitStatus confit_v2_structure_compile_choices_and_constraints(
    ConfitV2CompiledStructure *compiled, ConfitDiagnostic *diagnostic);

ConfitStatus confit_v2_structure_build_graphs(ConfitV2CompiledStructure *compiled,
                                               ConfitDiagnostic *diagnostic);

void confit_v2_structure_graphs_clear(ConfitV2CompiledStructure *compiled);

#endif /* CONFIT_CONSTRAINT_V2_COMPILED_INTERNAL_H */
