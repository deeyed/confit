#ifndef CONFIT_INPUT_INTERNAL_H
#define CONFIT_INPUT_INTERNAL_H

#include "confit/input.h"

/*
 * Transactionally transfer one already-read host buffer into an input image.
 * Success empties `buffer`; failure leaves it unchanged.  This seam lets the
 * controlled replacement regression separate pathname mutation from parsing
 * without adding a product callback or a second file open.
 */
ConfitStatus confit_input_image_from_host_buffer(
    const char *display_path, ConfitHostBuffer *buffer,
    const ConfitAllocator *allocator, ConfitInputImage **out_image,
    ConfitDiagnostic *diagnostic);

ConfitStatus confit_input_load_toml_at(
    ConfitHostRoot *root, const char *relative_path, const char *display_path,
    const ConfitAllocator *allocator, ConfitInputImage **out_image,
    ConfitDiagnostic *diagnostic);

#endif /* CONFIT_INPUT_INTERNAL_H */
