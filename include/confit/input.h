#ifndef CONFIT_INPUT_H
#define CONFIT_INPUT_H

#include <stddef.h>

#include "confit/diagnostic.h"
#include "confit/host.h"
#include "confit/model.h"
#include "confit/status.h"
#include "confit/toml.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Opaque owner of one exact configuration input byte image. */
typedef struct ConfitInputImage ConfitInputImage;

/**
 * @brief Open, read, identify, hash, index, and parse one TOML input exactly once.
 *
 * `relative_path` is resolved beneath `root` by the descriptor-rooted host
 * boundary.  The file is read with the per-TOML public ceiling.  Parsing and
 * SHA-256 consume that same owned byte image; neither step reopens the path.
 * On failure `out_image` is null and no partial image is published.
 */
ConfitStatus confit_input_load_toml(
    ConfitHostRoot *root, const char *relative_path,
    const ConfitAllocator *allocator, ConfitInputImage **out_image,
    ConfitDiagnostic *diagnostic);

/**
 * @brief Load one explicitly named absolute TOML path without discovery.
 *
 * The path is normalized and split into an absolute directory capability plus
 * one relative leaf walk.  The owned input image retains the complete absolute
 * display path so a sealed manifest can later remeasure the exact capability.
 */
ConfitStatus confit_input_load_toml_absolute(
    const char *absolute_path, const ConfitAllocator *allocator,
    ConfitInputImage **out_image, ConfitDiagnostic *diagnostic);

/**
 * @brief Release the parsed document, line index, exact bytes, and display path.
 *
 * Every pointer returned by an input-image accessor is borrowed until this
 * call.  Null is accepted.
 */
void confit_input_image_destroy(ConfitInputImage *image);

/** @brief Borrow the normalized display path owned by the image. */
const char *confit_input_image_path(const ConfitInputImage *image);

/** @brief Borrow the exact immutable input bytes and return their byte count. */
const unsigned char *confit_input_image_bytes(const ConfitInputImage *image,
                                              size_t *out_size);

/** @brief Borrow the lowercase SHA-256 of the exact input bytes. */
const char *confit_input_image_digest(const ConfitInputImage *image);

/** @brief Copy the identity of the regular file from which the bytes came. */
int confit_input_image_identity(const ConfitInputImage *image,
                                ConfitHostFileIdentity *out_identity);

/** @brief Borrow the read-only TOML document parsed from the exact bytes. */
const ConfitTomlDocument *
confit_input_image_document(const ConfitInputImage *image);

/** @brief Return the number of indexed 1-based source lines. */
size_t confit_input_image_line_count(const ConfitInputImage *image);

/**
 * @brief Convert a byte offset to a 1-based line and byte column.
 *
 * `byte_offset == image_size` addresses EOF.  A CRLF sequence counts as two
 * bytes; the next line begins after LF.  Invalid offsets leave outputs
 * unchanged and return zero.
 */
int confit_input_image_locate(const ConfitInputImage *image,
                              size_t byte_offset, size_t *out_line,
                              size_t *out_column);

/**
 * @brief Add this image to a project-loader total without overflow.
 *
 * The result is limited by `CONFIT_LIMIT_TOTAL_INPUT_BYTES`.  Failure leaves
 * `out_total` unchanged.  This is accounting only and does not load a graph.
 */
ConfitStatus confit_input_image_accumulate(
    size_t current_total, const ConfitInputImage *image, size_t *out_total,
    ConfitDiagnostic *diagnostic);

#ifdef __cplusplus
}
#endif

#endif /* CONFIT_INPUT_H */
