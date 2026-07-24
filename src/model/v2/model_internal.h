#ifndef CONFIT_MODEL_V2_INTERNAL_H
#define CONFIT_MODEL_V2_INTERNAL_H

#include "confit/schema_v2.h"

void *confit_v2_allocate(const ConfitV2Allocator *allocator, size_t size);
void *confit_v2_reallocate(const ConfitV2Allocator *allocator, void *allocation,
                           size_t size);
void confit_v2_deallocate(const ConfitV2Allocator *allocator,
                          void *allocation);
char *confit_v2_strdup(const ConfitV2Allocator *allocator, const char *text);
int confit_v2_allocator_is_valid(const ConfitV2Allocator *allocator);
const ConfitV2Allocator *confit_v2_default_allocator(void);
void confit_v2_source_span_clear(const ConfitV2Allocator *allocator,
                                 ConfitV2SourceSpan *span);
void confit_v2_value_clear(const ConfitV2Allocator *allocator,
                           ConfitV2Value *value);
void confit_v2_string_list_clear(const ConfitV2Allocator *allocator,
                                 ConfitV2StringList *list);

#endif /* CONFIT_MODEL_V2_INTERNAL_H */
