#ifndef CONFIT_SCHEMA_DISPATCH_INTERNAL_H
#define CONFIT_SCHEMA_DISPATCH_INTERNAL_H

#include "confit/model.h"
#include "confit/project.h"

ConfitProjectHandle *confit_project_handle_create_v1(ConfitProject *project);
const ConfitProject *
confit_project_handle_borrow_v1(const ConfitProjectHandle *project);

#endif /* CONFIT_SCHEMA_DISPATCH_INTERNAL_H */
