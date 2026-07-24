#ifndef CONFIT_SCHEMA_DISPATCH_INTERNAL_H
#define CONFIT_SCHEMA_DISPATCH_INTERNAL_H

#include "confit/model.h"
#include "confit/project.h"
#include "confit/schema_v2.h"

ConfitProjectHandle *confit_project_handle_create_v1(ConfitProject *project);
ConfitProjectHandle *confit_project_handle_create_v2(ConfitV2Project *project);
const ConfitProject *
confit_project_handle_borrow_v1(const ConfitProjectHandle *project);
const ConfitV2Project *
confit_project_handle_borrow_v2(const ConfitProjectHandle *project);

#endif /* CONFIT_SCHEMA_DISPATCH_INTERNAL_H */
