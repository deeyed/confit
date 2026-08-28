#include "confit/config.h"

#include <stdint.h>
#include <string.h>

#include "confit/limits.h"
#include "confit/schema.h"
#include "toml_internal.h"

struct ConfitUserConfig {
  ConfitAllocator allocator;
  ConfitUserDocument *document;
  ConfitAssignment *assignments;
  size_t assignment_count;
};

typedef struct ConfitTomlSink {
  char *bytes;
  size_t capacity;
  size_t used;
  int failed;
} ConfitTomlSink;

static const char kInvalidArgument[] = "invalid user configuration argument";
static const char kInvalidAllocator[] = "allocator capability is incomplete";
static const char kOutOfMemory[] = "failed to allocate user configuration";
static const char kUnknownSymbol[] =
    "user value names an unknown or stale configuration symbol";
static const char kDuplicateSymbol[] = "user value symbol is duplicated";
static const char kWrongType[] =
    "user value does not match the declared native TOML type";
static const char kInvalidResolution[] =
    "minimal user serialization requires a valid resolved configuration";
static const char kBufferTooSmall[] =
    "minimal user configuration buffer is too small";
static const char kSerializedTooLarge[] =
    "minimal user configuration exceeds the TOML file limit";
static const char kUnsafeValue[] =
    "resolved value cannot be represented as schema 6 user TOML";

static ConfitStatus confit_user_config_fail(
    ConfitDiagnostic *diagnostic, ConfitStatus status,
    const ConfitSourceSpan *span, const char *message) {
  confit_diagnostic_set(diagnostic, status,
                        span != 0 ? span->path : 0,
                        span != 0 ? span->line : 0U,
                        span != 0 ? span->column : 0U, message);
  return status;
}

static int confit_user_config_allocator(const ConfitAllocator *requested,
                                        ConfitAllocator *resolved) {
  if (resolved == 0) return 0;
  if (requested == 0) {
    confit_allocator_default(resolved);
    return 1;
  }
  if (!confit_allocator_is_valid(requested)) return 0;
  *resolved = *requested;
  return 1;
}

static int confit_user_config_size_multiply(size_t left, size_t right,
                                            size_t *out) {
  if (out == 0 || (left != 0U && right > SIZE_MAX / left)) return 0;
  *out = left * right;
  return 1;
}

static int confit_user_config_toml_path(const char *path) {
  const size_t size = path != 0 ? strlen(path) : 0U;
  return size >= 5U && memcmp(path + size - 5U, ".toml", 5U) == 0;
}

static void confit_user_config_destroy_assignments(
    ConfitAssignment *assignments, size_t count,
    const ConfitAllocator *allocator) {
  size_t index;
  if (assignments == 0) return;
  for (index = count; index > 0U; --index)
    confit_assignment_destroy(&assignments[index - 1U]);
  allocator->deallocate(allocator->context, assignments);
}

void confit_user_config_destroy(ConfitUserConfig *config) {
  ConfitAllocator allocator;
  if (config == 0) return;
  allocator = config->allocator;
  confit_user_config_destroy_assignments(config->assignments,
                                         config->assignment_count,
                                         &allocator);
  confit_user_document_destroy(config->document);
  memset(config, 0, sizeof(*config));
  allocator.deallocate(allocator.context, config);
}

static ConfitStatus confit_user_config_parse_value(
    const ConfitUserDocument *document, const ConfitUserValueView *user,
    const ConfitConfigView *declaration, const ConfitAllocator *allocator,
    ConfitValue *out_value, ConfitDiagnostic *diagnostic) {
  const ConfitTomlDocument *toml;
  const char *text = 0;
  size_t text_size = 0U;
  int boolean = 0;
  int64_t integer = 0;
  ConfitTomlIntegerBase base = CONFIT_TOML_INTEGER_BASE_UNKNOWN;
  ConfitStatus status = CONFIT_ERR_VALIDATION;

  toml = confit_input_image_document(confit_user_document_input(document));
  switch (declaration->kind) {
  case CONFIT_VALUE_BOOL:
    if (confit_toml_value_bool(user->value_candidate, &boolean))
      status = confit_value_set_bool(out_value, boolean, allocator, diagnostic);
    break;
  case CONFIT_VALUE_INT:
    if (confit_toml_value_int64(user->value_candidate, &integer))
      status = confit_value_set_int(out_value, integer, allocator, diagnostic);
    break;
  case CONFIT_VALUE_HEX:
    if (confit_toml_value_int64(user->value_candidate, &integer) &&
        integer >= 0 &&
        confit_toml_integer_base_from_image(toml, user->value_candidate,
                                            &base) &&
        base == CONFIT_TOML_INTEGER_BASE_HEXADECIMAL) {
      status = confit_value_set_hex(out_value, (uint64_t)integer, allocator,
                                    diagnostic);
    }
    break;
  case CONFIT_VALUE_STRING:
    if (confit_toml_value_string(user->value_candidate, &text, &text_size))
      status = confit_value_set_string(out_value, text, text_size, allocator,
                                       diagnostic);
    break;
  case CONFIT_VALUE_ENUM:
    if (confit_toml_value_string(user->value_candidate, &text, &text_size))
      status = confit_value_set_enum(out_value, text, text_size, allocator,
                                     diagnostic);
    break;
  case CONFIT_VALUE_INVALID:
  default:
    status = CONFIT_ERR_INTERNAL;
    break;
  }
  if (status == CONFIT_OK) return CONFIT_OK;
  return confit_user_config_fail(
      diagnostic, status == CONFIT_ERR_INTERNAL ? CONFIT_ERR_INTERNAL
                                                : CONFIT_ERR_VALIDATION,
      &user->declaration,
      status == CONFIT_ERR_INTERNAL ? kOutOfMemory : kWrongType);
}

static void confit_user_config_sort_assignments(
    ConfitAssignment *assignments, ConfitAssignment *scratch, size_t count) {
  size_t width;
  ConfitAssignment *source = assignments;
  ConfitAssignment *destination = scratch;
  if (count < 2U) return;
  for (width = 1U; width < count;) {
    size_t start;
    for (start = 0U; start < count; start += width * 2U) {
      const size_t middle = start + width < count ? start + width : count;
      const size_t end = middle + width < count ? middle + width : count;
      size_t left = start;
      size_t right = middle;
      size_t output = start;
      while (left < middle && right < end) {
        if (strcmp(source[right].symbol, source[left].symbol) < 0)
          destination[output++] = source[right++];
        else
          destination[output++] = source[left++];
      }
      while (left < middle) destination[output++] = source[left++];
      while (right < end) destination[output++] = source[right++];
    }
    {
      ConfitAssignment *swap = source;
      source = destination;
      destination = swap;
    }
    if (width > count / 2U) break;
    width *= 2U;
  }
  if (source != assignments)
    memcpy(assignments, source, count * sizeof(*assignments));
}

static ConfitStatus confit_user_config_load(
    ConfitHostRoot *project_root, const char *path, int absolute,
    const ConfitCatalog *catalog, const ConfitAllocator *allocator,
    ConfitUserConfig **out_config, ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitUserConfig *config = 0;
  ConfitAssignment *scratch = 0;
  size_t count;
  size_t bytes = 0U;
  size_t index;
  ConfitStatus status;

  if ((!absolute && project_root == 0) || path == 0 || catalog == 0 ||
      out_config == 0)
    return confit_user_config_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                   kInvalidArgument);
  *out_config = 0;
  if (!confit_user_config_allocator(allocator, &resolved))
    return confit_user_config_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                   kInvalidAllocator);
  config = (ConfitUserConfig *)resolved.allocate(resolved.context,
                                                  sizeof(*config));
  if (config == 0)
    return confit_user_config_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                   kOutOfMemory);
  memset(config, 0, sizeof(*config));
  config->allocator = resolved;
  status = absolute
               ? confit_user_document_load_absolute(
                     path, &resolved, &config->document, diagnostic)
               : confit_user_document_load_relative(
                     project_root, path, &resolved, &config->document,
                     diagnostic);
  if (status != CONFIT_OK) goto fail;
  count = confit_user_document_value_count(config->document);
  if (count != 0U) {
    if (!confit_user_config_size_multiply(count, sizeof(*config->assignments),
                                          &bytes)) {
      status = confit_user_config_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                       kOutOfMemory);
      goto fail;
    }
    config->assignments = (ConfitAssignment *)resolved.allocate(
        resolved.context, bytes);
    scratch = (ConfitAssignment *)resolved.allocate(resolved.context, bytes);
    if (config->assignments == 0 || scratch == 0) {
      status = confit_user_config_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                       kOutOfMemory);
      goto fail;
    }
    memset(config->assignments, 0, bytes);
    memset(scratch, 0, bytes);
    for (index = 0U; index < count; ++index)
      confit_assignment_init(&config->assignments[index]);
  }
  for (index = 0U; index < count; ++index) {
    ConfitUserValueView user;
    ConfitConfigView declaration;
    ConfitValue value;
    if (!confit_user_document_value_at(config->document, index, &user)) {
      status = confit_user_config_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                       kInvalidArgument);
      goto fail;
    }
    if (!confit_catalog_find_config(catalog, user.symbol, &declaration)) {
      status = confit_user_config_fail(diagnostic, CONFIT_ERR_VALIDATION,
                                       &user.declaration, kUnknownSymbol);
      goto fail;
    }
    confit_value_init(&value);
    status = confit_user_config_parse_value(config->document, &user,
                                            &declaration, &resolved, &value,
                                            diagnostic);
    if (status == CONFIT_OK)
      status = confit_assignment_set(&config->assignments[index], user.symbol,
                                     &value, &resolved, diagnostic);
    confit_value_destroy(&value);
    if (status != CONFIT_OK) {
      (void)confit_user_config_fail(diagnostic, status, &user.declaration,
                                    status == CONFIT_ERR_INTERNAL
                                        ? kOutOfMemory
                                        : kWrongType);
      goto fail;
    }
    config->assignment_count += 1U;
  }
  confit_user_config_sort_assignments(config->assignments, scratch, count);
  for (index = 1U; index < count; ++index) {
    if (strcmp(config->assignments[index - 1U].symbol,
               config->assignments[index].symbol) == 0) {
      status = confit_user_config_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                                       kDuplicateSymbol);
      goto fail;
    }
  }
  if (scratch != 0) resolved.deallocate(resolved.context, scratch);
  *out_config = config;
  return CONFIT_OK;

fail:
  if (diagnostic != 0) (void)confit_diagnostic_stabilize_path(diagnostic);
  if (scratch != 0) resolved.deallocate(resolved.context, scratch);
  confit_user_config_destroy(config);
  return status;
}

ConfitStatus confit_user_config_load_relative(
    ConfitHostRoot *project_root, const char *path,
    const ConfitCatalog *catalog, const ConfitAllocator *allocator,
    ConfitUserConfig **out_config, ConfitDiagnostic *diagnostic) {
  return confit_user_config_load(project_root, path, 0, catalog, allocator,
                                 out_config, diagnostic);
}

ConfitStatus confit_user_config_load_absolute(
    const char *absolute_path, const ConfitCatalog *catalog,
    const ConfitAllocator *allocator, ConfitUserConfig **out_config,
    ConfitDiagnostic *diagnostic) {
  return confit_user_config_load(0, absolute_path, 1, catalog, allocator,
                                 out_config, diagnostic);
}

const ConfitInputImage *
confit_user_config_input(const ConfitUserConfig *config) {
  return config != 0 ? confit_user_document_input(config->document) : 0;
}

size_t confit_user_config_assignment_count(const ConfitUserConfig *config) {
  return config != 0 ? config->assignment_count : 0U;
}

int confit_user_config_assignment_at(const ConfitUserConfig *config,
                                     size_t lexical_index,
                                     const ConfitAssignment **out_assignment) {
  if (config == 0 || out_assignment == 0 ||
      lexical_index >= config->assignment_count)
    return 0;
  *out_assignment = &config->assignments[lexical_index];
  return 1;
}

const ConfitAssignment *
confit_user_config_assignments(const ConfitUserConfig *config,
                               size_t *out_count) {
  if (out_count == 0) return 0;
  *out_count = config != 0 ? config->assignment_count : 0U;
  return config != 0 ? config->assignments : 0;
}

static int confit_toml_sink_append(ConfitTomlSink *sink,
                                   const char *bytes, size_t size) {
  if (sink == 0 || (bytes == 0 && size != 0U) ||
      sink->used > CONFIT_LIMIT_TOML_FILE_BYTES ||
      size > CONFIT_LIMIT_TOML_FILE_BYTES - sink->used ||
      (sink->bytes != 0 &&
       (sink->used > sink->capacity || size > sink->capacity - sink->used))) {
    if (sink != 0) sink->failed = 1;
    return 0;
  }
  if (sink->bytes != 0 && size != 0U)
    memcpy(sink->bytes + sink->used, bytes, size);
  sink->used += size;
  return 1;
}

static int confit_toml_sink_unsigned(ConfitTomlSink *sink, uint64_t value,
                                     unsigned base) {
  static const char digits[] = "0123456789abcdef";
  char reversed[32];
  char forward[32];
  size_t count = 0U;
  size_t index;
  do {
    reversed[count++] = digits[value % base];
    value /= base;
  } while (value != 0U);
  for (index = 0U; index < count; ++index)
    forward[index] = reversed[count - index - 1U];
  return confit_toml_sink_append(sink, forward, count);
}

static int confit_toml_sink_integer(ConfitTomlSink *sink, int64_t value) {
  uint64_t magnitude;
  if (value < 0) {
    if (!confit_toml_sink_append(sink, "-", 1U)) return 0;
    magnitude = (uint64_t)(-(value + INT64_C(1))) + UINT64_C(1);
  } else {
    magnitude = (uint64_t)value;
  }
  return confit_toml_sink_unsigned(sink, magnitude, 10U);
}

static int confit_toml_sink_string(ConfitTomlSink *sink,
                                   const char *text, size_t size) {
  size_t index;
  if (text == 0 || !confit_toml_sink_append(sink, "\"", 1U)) return 0;
  for (index = 0U; index < size; ++index) {
    const unsigned char byte = (unsigned char)text[index];
    const char *escape = 0;
    if (byte == (unsigned char)'\"') escape = "\\\"";
    else if (byte == (unsigned char)'\\') escape = "\\\\";
    else if (byte == (unsigned char)'\t') escape = "\\t";
    else if (byte == (unsigned char)'\n') escape = "\\n";
    else if (byte == (unsigned char)'\r') escape = "\\r";
    else if (byte < 0x20U || byte == 0x7FU || byte == 0x1BU)
      return 0;
    if (escape != 0) {
      if (!confit_toml_sink_append(sink, escape, 2U)) return 0;
    } else if (!confit_toml_sink_append(sink, text + index, 1U)) {
      return 0;
    }
  }
  return confit_toml_sink_append(sink, "\"", 1U);
}

static int confit_toml_sink_value(ConfitTomlSink *sink,
                                  const ConfitValue *value) {
  if (value == 0) return 0;
  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    if (value->data.boolean == 0)
      return confit_toml_sink_append(sink, "false", 5U);
    if (value->data.boolean == 1)
      return confit_toml_sink_append(sink, "true", 4U);
    return 0;
  case CONFIT_VALUE_INT:
    return confit_toml_sink_integer(sink, value->data.integer);
  case CONFIT_VALUE_HEX:
    return value->data.hexadecimal <= UINT64_C(0x7fffffffffffffff) &&
           confit_toml_sink_append(sink, "0x", 2U) &&
           confit_toml_sink_unsigned(sink, value->data.hexadecimal, 16U);
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
    return confit_toml_sink_string(sink, value->data.text.data,
                                   value->data.text.size);
  case CONFIT_VALUE_INVALID:
  default:
    return 0;
  }
}

static ConfitStatus confit_user_config_emit_minimal(
    const ConfitResolution *resolution, ConfitTomlSink *sink,
    ConfitDiagnostic *diagnostic) {
  static const char prefix[] = "schema_version = 6\n\n[values]\n";
  size_t count;
  size_t index;
  if (resolution == 0 || sink == 0 ||
      confit_resolution_catalog(resolution) == 0)
    return confit_user_config_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                   kInvalidResolution);
  if (!confit_toml_sink_append(sink, prefix, sizeof(prefix) - 1U))
    return confit_user_config_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                                   kSerializedTooLarge);
  count = confit_resolution_value_count(resolution);
  for (index = 0U; index < count; ++index) {
    const ConfitResolvedValue *value = 0;
    if (!confit_resolution_value_at(resolution, index, &value) ||
        value == 0 || value->symbol == 0 ||
        (value->origin != CONFIT_ORIGIN_DEFAULT &&
         value->origin != CONFIT_ORIGIN_USER)) {
      return confit_user_config_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                     kInvalidResolution);
    }
    if (value->origin != CONFIT_ORIGIN_USER ||
        confit_value_equal(&value->default_value, &value->effective_value))
      continue;
    if (!value->available)
      return confit_user_config_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                     kInvalidResolution);
    if (!confit_toml_sink_append(sink, value->symbol, strlen(value->symbol)) ||
        !confit_toml_sink_append(sink, " = ", 3U) ||
        !confit_toml_sink_value(sink, &value->effective_value) ||
        !confit_toml_sink_append(sink, "\n", 1U)) {
      return confit_user_config_fail(
          diagnostic,
          sink->failed ? CONFIT_ERR_VALIDATION : CONFIT_ERR_INTERNAL, 0,
          sink->failed ? kSerializedTooLarge : kUnsafeValue);
    }
  }
  return CONFIT_OK;
}

ConfitStatus confit_user_config_format_minimal(
    const ConfitResolution *resolution, char *buffer, size_t buffer_size,
    size_t *out_size, ConfitDiagnostic *diagnostic) {
  ConfitTomlSink sizing;
  ConfitTomlSink writing;
  ConfitStatus status;
  if (out_size == 0 || (buffer == 0 && buffer_size != 0U))
    return confit_user_config_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                   kInvalidArgument);
  *out_size = 0U;
  memset(&sizing, 0, sizeof(sizing));
  status = confit_user_config_emit_minimal(resolution, &sizing, diagnostic);
  if (status != CONFIT_OK) return status;
  *out_size = sizing.used;
  if (buffer == 0 && buffer_size == 0U) return CONFIT_OK;
  if (buffer_size <= sizing.used)
    return confit_user_config_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                   kBufferTooSmall);
  memset(&writing, 0, sizeof(writing));
  writing.bytes = buffer;
  writing.capacity = buffer_size;
  status = confit_user_config_emit_minimal(resolution, &writing, diagnostic);
  if (status != CONFIT_OK || writing.used != sizing.used)
    return status != CONFIT_OK
               ? status
               : confit_user_config_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                         kInvalidResolution);
  buffer[writing.used] = '\0';
  return CONFIT_OK;
}

ConfitStatus confit_user_config_write_minimal(
    ConfitHostRoot *destination_root, const char *destination_path,
    const ConfitResolution *resolution, const ConfitAllocator *allocator,
    ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  char *bytes;
  size_t size;
  ConfitStatus status;
  if (destination_root == 0 || destination_path == 0 || resolution == 0)
    return confit_user_config_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                   kInvalidArgument);
  if (!confit_host_relative_path_is_valid(destination_path) ||
      !confit_user_config_toml_path(destination_path))
    return confit_user_config_fail(diagnostic, CONFIT_ERR_VALIDATION, 0,
                                   kInvalidArgument);
  if (!confit_user_config_allocator(allocator, &resolved))
    return confit_user_config_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                                   kInvalidAllocator);
  status = confit_user_config_format_minimal(resolution, 0, 0U, &size,
                                             diagnostic);
  if (status != CONFIT_OK) return status;
  bytes = (char *)resolved.allocate(resolved.context, size + 1U);
  if (bytes == 0)
    return confit_user_config_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                                   kOutOfMemory);
  status = confit_user_config_format_minimal(resolution, bytes, size + 1U,
                                             &size, diagnostic);
  if (status == CONFIT_OK)
    status = confit_host_atomic_replace(destination_root, destination_path,
                                        bytes, size, 0644U, diagnostic);
  resolved.deallocate(resolved.context, bytes);
  return status;
}
