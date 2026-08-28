#include "confit/input.h"

#include <stdint.h>
#include <string.h>

#include "confit/digest.h"
#include "confit/limits.h"
#include "input_internal.h"
#include "toml_internal.h"

struct ConfitInputImage {
  ConfitAllocator allocator;
  char *display_path;
  ConfitHostBuffer buffer;
  char digest[65];
  size_t *line_starts;
  size_t line_count;
  ConfitTomlDocument *document;
};

static const char kInvalidArgument[] = "invalid input-image argument";
static const char kInvalidAllocator[] = "allocator capability is incomplete";
static const char kInvalidPath[] = "input display path is not normalized";
static const char kOutOfMemory[] = "failed to allocate an input image";
static const char kEmbeddedNul[] = "TOML input contains an embedded NUL byte";
static const char kTotalLimit[] = "configuration input exceeds the total byte limit";

static ConfitStatus confit_input_fail(ConfitDiagnostic *diagnostic,
                                      ConfitStatus status, const char *path,
                                      size_t line, size_t column,
                                      const char *message) {
  confit_diagnostic_set(diagnostic, status, path, line, column, message);
  return status;
}

static int confit_input_resolve_allocator(const ConfitAllocator *requested,
                                          ConfitAllocator *resolved) {
  if (resolved == 0) {
    return 0;
  }
  if (requested == 0) {
    confit_allocator_default(resolved);
    return 1;
  }
  if (!confit_allocator_is_valid(requested)) {
    return 0;
  }
  *resolved = *requested;
  return 1;
}

static void confit_input_candidate_destroy(ConfitInputImage *image,
                                           int destroy_buffer) {
  ConfitAllocator allocator;
  if (image == 0) {
    return;
  }
  allocator = image->allocator;
  confit_toml_document_free(image->document);
  if (destroy_buffer) {
    confit_host_buffer_destroy(&image->buffer);
  }
  if (image->line_starts != 0) {
    allocator.deallocate(allocator.context, image->line_starts);
  }
  if (image->display_path != 0) {
    allocator.deallocate(allocator.context, image->display_path);
  }
  memset(image, 0, sizeof(*image));
  allocator.deallocate(allocator.context, image);
}

void confit_input_image_destroy(ConfitInputImage *image) {
  confit_input_candidate_destroy(image, 1);
}

static int confit_input_copy_path(ConfitInputImage *image, const char *path) {
  const size_t size = strlen(path);
  image->display_path =
      (char *)image->allocator.allocate(image->allocator.context, size + 1U);
  if (image->display_path == 0) {
    return 0;
  }
  memcpy(image->display_path, path, size + 1U);
  return 1;
}

static int confit_input_find_nul(const unsigned char *bytes, size_t size,
                                 size_t *out_offset) {
  size_t index;
  for (index = 0U; index < size; ++index) {
    if (bytes[index] == 0U) {
      if (out_offset != 0) {
        *out_offset = index;
      }
      return 1;
    }
  }
  return 0;
}

static int confit_input_build_line_index(ConfitInputImage *image,
                                         const unsigned char *bytes,
                                         size_t size) {
  size_t count = 1U;
  size_t index;
  size_t cursor = 1U;

  for (index = 0U; index < size; ++index) {
    if (bytes[index] == (unsigned char)'\n') {
      if (count == SIZE_MAX) {
        return 0;
      }
      count += 1U;
    }
  }
  if (count > SIZE_MAX / sizeof(*image->line_starts)) {
    return 0;
  }
  image->line_starts = (size_t *)image->allocator.allocate(
      image->allocator.context, count * sizeof(*image->line_starts));
  if (image->line_starts == 0) {
    return 0;
  }
  image->line_starts[0] = 0U;
  for (index = 0U; index < size; ++index) {
    if (bytes[index] == (unsigned char)'\n') {
      image->line_starts[cursor++] = index + 1U;
    }
  }
  image->line_count = count;
  return cursor == count;
}

static void confit_input_locate_raw(const unsigned char *bytes, size_t offset,
                                    size_t *out_line, size_t *out_column) {
  size_t index;
  size_t line = 1U;
  size_t column = 1U;
  for (index = 0U; index < offset; ++index) {
    if (bytes[index] == (unsigned char)'\n') {
      line += 1U;
      column = 1U;
    } else {
      column += 1U;
    }
  }
  *out_line = line;
  *out_column = column;
}

ConfitStatus confit_input_image_from_host_buffer(
    const char *display_path, ConfitHostBuffer *buffer,
    const ConfitAllocator *allocator, ConfitInputImage **out_image,
    ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitInputImage *candidate;
  size_t nul_offset;
  size_t line;
  size_t column;
  ConfitStatus status;

  if (display_path == 0 || buffer == 0 || out_image == 0 ||
      (buffer->bytes == 0 && buffer->size != 0U)) {
    return confit_input_fail(diagnostic, CONFIT_ERR_USAGE, display_path, 0U,
                             0U, kInvalidArgument);
  }
  *out_image = 0;
  if (!confit_host_relative_path_is_valid(display_path)) {
    return confit_input_fail(diagnostic, CONFIT_ERR_IO, display_path, 0U, 0U,
                             kInvalidPath);
  }
  if (buffer->bytes == 0 || buffer->size > CONFIT_LIMIT_TOML_FILE_BYTES) {
    return confit_input_fail(diagnostic, CONFIT_ERR_VALIDATION, display_path,
                             0U, 0U, kInvalidArgument);
  }
  if (!confit_input_resolve_allocator(allocator, &resolved)) {
    return confit_input_fail(diagnostic, CONFIT_ERR_USAGE, display_path, 0U,
                             0U, kInvalidAllocator);
  }
  if (confit_input_find_nul(buffer->bytes, buffer->size, &nul_offset)) {
    confit_input_locate_raw(buffer->bytes, nul_offset, &line, &column);
    return confit_input_fail(diagnostic, CONFIT_ERR_VALIDATION, display_path,
                             line, column, kEmbeddedNul);
  }

  candidate = (ConfitInputImage *)resolved.allocate(resolved.context,
                                                     sizeof(*candidate));
  if (candidate == 0) {
    return confit_input_fail(diagnostic, CONFIT_ERR_INTERNAL, display_path, 0U,
                             0U, kOutOfMemory);
  }
  memset(candidate, 0, sizeof(*candidate));
  candidate->allocator = resolved;
  confit_host_buffer_init(&candidate->buffer);

  if (!confit_input_copy_path(candidate, display_path) ||
      !confit_input_build_line_index(candidate, buffer->bytes, buffer->size)) {
    confit_input_candidate_destroy(candidate, 0);
    return confit_input_fail(diagnostic, CONFIT_ERR_INTERNAL, display_path, 0U,
                             0U, kOutOfMemory);
  }

  confit_sha256_bytes(buffer->bytes, buffer->size, candidate->digest);
  status = confit_toml_parse_borrowed(
      display_path, (const char *)buffer->bytes, buffer->size,
      &candidate->document, diagnostic);
  if (status != CONFIT_OK) {
    confit_input_candidate_destroy(candidate, 0);
    if (diagnostic != 0) {
      diagnostic->path = display_path;
    }
    return status;
  }

  candidate->buffer = *buffer;
  confit_host_buffer_init(buffer);
  *out_image = candidate;
  return CONFIT_OK;
}

ConfitStatus confit_input_load_toml(
    ConfitHostRoot *root, const char *relative_path,
    const ConfitAllocator *allocator, ConfitInputImage **out_image,
    ConfitDiagnostic *diagnostic) {
  ConfitHostBuffer buffer;
  ConfitHostFile *file = 0;
  ConfitStatus status;

  if (root == 0 || relative_path == 0 || out_image == 0) {
    return confit_input_fail(diagnostic, CONFIT_ERR_USAGE, relative_path, 0U,
                             0U, kInvalidArgument);
  }
  *out_image = 0;
  confit_host_buffer_init(&buffer);
  status = confit_host_file_open(root, relative_path, &file, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_host_file_read(file, CONFIT_LIMIT_TOML_FILE_BYTES,
                                   allocator, &buffer, diagnostic);
  }
  confit_host_file_destroy(file);
  if (status == CONFIT_OK) {
    status = confit_input_image_from_host_buffer(
        relative_path, &buffer, allocator, out_image, diagnostic);
  }
  if (status != CONFIT_OK && diagnostic != 0 && diagnostic->path == 0) {
    diagnostic->path = relative_path;
  }
  confit_host_buffer_destroy(&buffer);
  return status;
}

const char *confit_input_image_path(const ConfitInputImage *image) {
  return image != 0 ? image->display_path : 0;
}

const unsigned char *confit_input_image_bytes(const ConfitInputImage *image,
                                              size_t *out_size) {
  if (image == 0 || out_size == 0) {
    return 0;
  }
  *out_size = image->buffer.size;
  return image->buffer.bytes;
}

const char *confit_input_image_digest(const ConfitInputImage *image) {
  return image != 0 ? image->digest : 0;
}

int confit_input_image_identity(const ConfitInputImage *image,
                                ConfitHostFileIdentity *out_identity) {
  if (image == 0 || out_identity == 0) {
    return 0;
  }
  *out_identity = image->buffer.identity;
  return 1;
}

const ConfitTomlDocument *
confit_input_image_document(const ConfitInputImage *image) {
  return image != 0 ? image->document : 0;
}

size_t confit_input_image_line_count(const ConfitInputImage *image) {
  return image != 0 ? image->line_count : 0U;
}

int confit_input_image_locate(const ConfitInputImage *image,
                              size_t byte_offset, size_t *out_line,
                              size_t *out_column) {
  size_t lower;
  size_t upper;
  if (image == 0 || out_line == 0 || out_column == 0 ||
      byte_offset > image->buffer.size || image->line_count == 0U) {
    return 0;
  }
  lower = 0U;
  upper = image->line_count;
  while (lower + 1U < upper) {
    const size_t middle = lower + (upper - lower) / 2U;
    if (image->line_starts[middle] <= byte_offset) {
      lower = middle;
    } else {
      upper = middle;
    }
  }
  *out_line = lower + 1U;
  *out_column = byte_offset - image->line_starts[lower] + 1U;
  return 1;
}

ConfitStatus confit_input_image_accumulate(
    size_t current_total, const ConfitInputImage *image, size_t *out_total,
    ConfitDiagnostic *diagnostic) {
  if (image == 0 || out_total == 0) {
    return confit_input_fail(diagnostic, CONFIT_ERR_USAGE,
                             image != 0 ? image->display_path : 0, 0U, 0U,
                             kInvalidArgument);
  }
  if (current_total > CONFIT_LIMIT_TOTAL_INPUT_BYTES ||
      image->buffer.size > CONFIT_LIMIT_TOTAL_INPUT_BYTES - current_total) {
    return confit_input_fail(diagnostic, CONFIT_ERR_VALIDATION,
                             image->display_path, 0U, 0U, kTotalLimit);
  }
  *out_total = current_total + image->buffer.size;
  return CONFIT_OK;
}
