#include "confit/emitter.h"

#include <stdint.h>
#include <string.h>

#include "confit/limits.h"

typedef struct ConfitEmitterArtifact {
  ConfitEmitterKind kind;
  const char *role;
  const char *name;
  unsigned char *bytes;
  size_t size;
} ConfitEmitterArtifact;

struct ConfitEmission {
  ConfitAllocator allocator;
  ConfitEmitterArtifact artifacts[3];
  size_t artifact_count;
};

typedef struct ConfitEmitterWriter {
  unsigned char *bytes;
  size_t size;
  size_t capacity;
} ConfitEmitterWriter;

typedef int (*ConfitEmitterFormat)(const ConfitResolution *resolution,
                                   ConfitEmitterWriter *writer);

static const char kInvalidArgument[] = "invalid emitter argument";
static const char kInvalidAllocator[] = "allocator capability is incomplete";
static const char kInvalidResolution[] =
    "resolved configuration is not safe to emit";
static const char kMakeStringUnsupported[] =
    "Make output does not support string configuration values";
static const char kTooLarge[] = "emitted artifact exceeds the public byte limit";
static const char kOutOfMemory[] = "failed to allocate an emitted artifact";

static ConfitStatus confit_emitter_fail(ConfitDiagnostic *diagnostic,
                                        ConfitStatus status,
                                        const char *path,
                                        const char *message) {
  confit_diagnostic_set(diagnostic, status, path, 0U, 0U, message);
  return status;
}

static int confit_emitter_resolve_allocator(const ConfitAllocator *requested,
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

static int confit_emitter_request_is_valid(const ConfitEmitRequest *request) {
  return request != 0 &&
         (request->emit_make == 0 || request->emit_make == 1) &&
         (request->emit_c_header == 0 || request->emit_c_header == 1) &&
         (request->emit_json == 0 || request->emit_json == 1) &&
         (request->emit_make || request->emit_c_header || request->emit_json);
}

static int confit_emitter_enum_atom_is_valid(const char *text, size_t size) {
  size_t index;
  if (text == 0 || size == 0U || size > CONFIT_LIMIT_ENUM_ATOM_BYTES)
    return 0;
  for (index = 0U; index < size; ++index) {
    const unsigned char byte = (unsigned char)text[index];
    if (!((byte >= 'A' && byte <= 'Z') ||
          (byte >= 'a' && byte <= 'z') ||
          (byte >= '0' && byte <= '9') || byte == '_' || byte == '.' ||
          byte == '+' || byte == '-'))
      return 0;
  }
  return text[size] == '\0';
}

static int confit_emitter_value_is_valid(const ConfitValue *value) {
  if (value == 0) return 0;
  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return value->data.boolean == 0 || value->data.boolean == 1;
  case CONFIT_VALUE_INT:
    return 1;
  case CONFIT_VALUE_HEX:
    return value->data.hexadecimal <= UINT64_C(0x7fffffffffffffff);
  case CONFIT_VALUE_STRING:
    return value->data.text.data != 0 &&
           value->data.text.size <= CONFIT_LIMIT_STRING_BYTES &&
           value->data.text.data[value->data.text.size] == '\0';
  case CONFIT_VALUE_ENUM:
    return confit_emitter_enum_atom_is_valid(value->data.text.data,
                                             value->data.text.size);
  case CONFIT_VALUE_INVALID:
  default:
    return 0;
  }
}

static ConfitStatus confit_emitter_preflight(
    const ConfitResolution *resolution, const ConfitEmitRequest *request,
    ConfitDiagnostic *diagnostic) {
  const size_t count = confit_resolution_value_count(resolution);
  const char *previous = 0;
  size_t index;
  for (index = 0U; index < count; ++index) {
    const ConfitResolvedValue *value = 0;
    if (!confit_resolution_value_at(resolution, index, &value) || value == 0 ||
        !confit_symbol_is_valid(value->symbol) ||
        !confit_emitter_value_is_valid(&value->default_value) ||
        !confit_emitter_value_is_valid(&value->effective_value) ||
        value->default_value.kind != value->effective_value.kind ||
        (value->origin != CONFIT_ORIGIN_DEFAULT &&
         value->origin != CONFIT_ORIGIN_USER) ||
        (value->available != 0 && value->available != 1) ||
        (previous != 0 && strcmp(previous, value->symbol) >= 0))
      return confit_emitter_fail(diagnostic, CONFIT_ERR_INTERNAL,
                                 value != 0 ? value->symbol : 0,
                                 kInvalidResolution);
    if (request->emit_make &&
        value->effective_value.kind == CONFIT_VALUE_STRING) {
      ConfitConfigView declaration;
      const char *path = 0;
      if (confit_catalog_find_config(
              confit_resolution_catalog(resolution), value->symbol,
              &declaration))
        path = declaration.declaration.path;
      return confit_emitter_fail(diagnostic, CONFIT_ERR_VALIDATION,
                                 path, kMakeStringUnsupported);
    }
    previous = value->symbol;
  }
  return CONFIT_OK;
}

static int confit_writer_append(ConfitEmitterWriter *writer,
                                const void *bytes, size_t size) {
  if (writer == 0 || (bytes == 0 && size != 0U) ||
      writer->size > CONFIT_LIMIT_SNAPSHOT_BYTES ||
      size > CONFIT_LIMIT_SNAPSHOT_BYTES - writer->size)
    return 0;
  if (writer->bytes != 0) {
    if (writer->size > writer->capacity ||
        size > writer->capacity - writer->size)
      return 0;
    if (size != 0U) memcpy(writer->bytes + writer->size, bytes, size);
  }
  writer->size += size;
  return 1;
}

static int confit_writer_text(ConfitEmitterWriter *writer, const char *text) {
  return text != 0 && confit_writer_append(writer, text, strlen(text));
}

static int confit_writer_unsigned(ConfitEmitterWriter *writer, uint64_t value,
                                  unsigned base) {
  static const char digits[] = "0123456789abcdef";
  char reverse[32];
  char forward[32];
  size_t count = 0U;
  size_t index;
  if (base != 10U && base != 16U) return 0;
  do {
    reverse[count++] = digits[value % base];
    value /= base;
  } while (value != 0U);
  for (index = 0U; index < count; ++index)
    forward[index] = reverse[count - index - 1U];
  return confit_writer_append(writer, forward, count);
}

static int confit_writer_integer(ConfitEmitterWriter *writer, int64_t value) {
  uint64_t magnitude;
  if (value < 0) {
    if (!confit_writer_text(writer, "-")) return 0;
    magnitude = (uint64_t)(-(value + INT64_C(1))) + UINT64_C(1);
  } else {
    magnitude = (uint64_t)value;
  }
  return confit_writer_unsigned(writer, magnitude, 10U);
}

static int confit_writer_json_string(ConfitEmitterWriter *writer,
                                     const char *text, size_t size) {
  static const char hex[] = "0123456789abcdef";
  size_t index;
  if (text == 0 || !confit_writer_text(writer, "\"")) return 0;
  for (index = 0U; index < size; ++index) {
    const unsigned char byte = (unsigned char)text[index];
    const char *escape = 0;
    char unicode[6];
    if (byte == (unsigned char)'\"') escape = "\\\"";
    else if (byte == (unsigned char)'\\') escape = "\\\\";
    else if (byte == (unsigned char)'\b') escape = "\\b";
    else if (byte == (unsigned char)'\f') escape = "\\f";
    else if (byte == (unsigned char)'\n') escape = "\\n";
    else if (byte == (unsigned char)'\r') escape = "\\r";
    else if (byte == (unsigned char)'\t') escape = "\\t";
    if (escape != 0) {
      if (!confit_writer_text(writer, escape)) return 0;
    } else if (byte < 0x20U || byte == 0x7FU) {
      unicode[0] = '\\';
      unicode[1] = 'u';
      unicode[2] = '0';
      unicode[3] = '0';
      unicode[4] = hex[byte >> 4U];
      unicode[5] = hex[byte & 0x0fU];
      if (!confit_writer_append(writer, unicode, sizeof(unicode))) return 0;
    } else if (!confit_writer_append(writer, text + index, 1U)) {
      return 0;
    }
  }
  return confit_writer_text(writer, "\"");
}

static int confit_writer_c_string(ConfitEmitterWriter *writer,
                                  const char *text, size_t size) {
  size_t index;
  if (text == 0 || !confit_writer_text(writer, "\"")) return 0;
  for (index = 0U; index < size; ++index) {
    const unsigned char byte = (unsigned char)text[index];
    char octal[4];
    if (byte == (unsigned char)'\"') {
      if (!confit_writer_text(writer, "\\\"")) return 0;
    } else if (byte == (unsigned char)'\\') {
      if (!confit_writer_text(writer, "\\\\")) return 0;
    } else if (byte >= 0x20U && byte <= 0x7eU &&
               byte != (unsigned char)'?') {
      if (!confit_writer_append(writer, text + index, 1U)) return 0;
    } else {
      octal[0] = '\\';
      octal[1] = (char)('0' + ((byte >> 6U) & 7U));
      octal[2] = (char)('0' + ((byte >> 3U) & 7U));
      octal[3] = (char)('0' + (byte & 7U));
      if (!confit_writer_append(writer, octal, sizeof(octal))) return 0;
    }
  }
  return confit_writer_text(writer, "\"");
}

static const char *confit_emitter_kind_name(ConfitValueKind kind) {
  switch (kind) {
  case CONFIT_VALUE_BOOL: return "bool";
  case CONFIT_VALUE_INT: return "int";
  case CONFIT_VALUE_HEX: return "hex";
  case CONFIT_VALUE_STRING: return "string";
  case CONFIT_VALUE_ENUM: return "enum";
  case CONFIT_VALUE_INVALID:
  default: return 0;
  }
}

static int confit_writer_json_value(ConfitEmitterWriter *writer,
                                    const ConfitValue *value) {
  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return confit_writer_text(writer,
                              value->data.boolean ? "true" : "false");
  case CONFIT_VALUE_INT:
    return confit_writer_integer(writer, value->data.integer);
  case CONFIT_VALUE_HEX:
    return confit_writer_text(writer, "\"0x") &&
           confit_writer_unsigned(writer, value->data.hexadecimal, 16U) &&
           confit_writer_text(writer, "\"");
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
    return confit_writer_json_string(writer, value->data.text.data,
                                     value->data.text.size);
  case CONFIT_VALUE_INVALID:
  default:
    return 0;
  }
}

static int confit_format_make(const ConfitResolution *resolution,
                              ConfitEmitterWriter *writer) {
  size_t index;
  for (index = 0U; index < confit_resolution_value_count(resolution);
       ++index) {
    const ConfitResolvedValue *value = 0;
    if (!confit_resolution_value_at(resolution, index, &value) ||
        !confit_writer_text(writer, "CONFIG_") ||
        !confit_writer_text(writer, value->symbol) ||
        !confit_writer_text(writer, "="))
      return 0;
    switch (value->effective_value.kind) {
    case CONFIT_VALUE_BOOL:
      if (!confit_writer_text(writer, value->effective_value.data.boolean
                                          ? "true"
                                          : "false"))
        return 0;
      break;
    case CONFIT_VALUE_INT:
      if (!confit_writer_integer(writer,
                                 value->effective_value.data.integer))
        return 0;
      break;
    case CONFIT_VALUE_HEX:
      if (!confit_writer_text(writer, "0x") ||
          !confit_writer_unsigned(writer,
                                  value->effective_value.data.hexadecimal,
                                  16U))
        return 0;
      break;
    case CONFIT_VALUE_ENUM:
      if (!confit_emitter_enum_atom_is_valid(
              value->effective_value.data.text.data,
              value->effective_value.data.text.size) ||
          !confit_writer_append(writer, value->effective_value.data.text.data,
                                value->effective_value.data.text.size))
        return 0;
      break;
    case CONFIT_VALUE_STRING:
    case CONFIT_VALUE_INVALID:
    default:
      return 0;
    }
    if (!confit_writer_text(writer, "\n")) return 0;
  }
  return 1;
}

static int confit_format_c_header(const ConfitResolution *resolution,
                                  ConfitEmitterWriter *writer) {
  size_t index;
  if (!confit_writer_text(writer,
                          "#ifndef CONFIT_GENERATED_VALUES_H\n"
                          "#define CONFIT_GENERATED_VALUES_H\n\n"))
    return 0;
  for (index = 0U; index < confit_resolution_value_count(resolution);
       ++index) {
    const ConfitResolvedValue *value = 0;
    if (!confit_resolution_value_at(resolution, index, &value) ||
        !confit_writer_text(writer, "#define CONFIG_") ||
        !confit_writer_text(writer, value->symbol) ||
        !confit_writer_text(writer, " "))
      return 0;
    switch (value->effective_value.kind) {
    case CONFIT_VALUE_BOOL:
      if (!confit_writer_text(writer,
                              value->effective_value.data.boolean ? "1" : "0"))
        return 0;
      break;
    case CONFIT_VALUE_INT:
      if (value->effective_value.data.integer == INT64_MIN) {
        if (!confit_writer_text(writer,
                                "(-9223372036854775807LL - 1LL)"))
          return 0;
      } else if (!confit_writer_integer(
                     writer, value->effective_value.data.integer)) {
        return 0;
      }
      break;
    case CONFIT_VALUE_HEX:
      if (!confit_writer_text(writer, "0x") ||
          !confit_writer_unsigned(writer,
                                  value->effective_value.data.hexadecimal,
                                  16U))
        return 0;
      break;
    case CONFIT_VALUE_STRING:
    case CONFIT_VALUE_ENUM:
      if (!confit_writer_c_string(writer,
                                  value->effective_value.data.text.data,
                                  value->effective_value.data.text.size))
        return 0;
      break;
    case CONFIT_VALUE_INVALID:
    default:
      return 0;
    }
    if (!confit_writer_text(writer, "\n")) return 0;
  }
  return confit_writer_text(writer, "\n#endif\n");
}

static int confit_format_json(const ConfitResolution *resolution,
                              ConfitEmitterWriter *writer) {
  size_t index;
  if (!confit_writer_text(writer, "{\"schema_version\":6,\"values\":["))
    return 0;
  for (index = 0U; index < confit_resolution_value_count(resolution);
       ++index) {
    const ConfitResolvedValue *value = 0;
    const char *kind;
    const char *origin;
    if (!confit_resolution_value_at(resolution, index, &value) ||
        (kind = confit_emitter_kind_name(value->effective_value.kind)) == 0)
      return 0;
    origin = value->origin == CONFIT_ORIGIN_USER ? "user" : "default";
    if ((index != 0U && !confit_writer_text(writer, ",")) ||
        !confit_writer_text(writer, "{\"symbol\":") ||
        !confit_writer_json_string(writer, value->symbol,
                                   strlen(value->symbol)) ||
        !confit_writer_text(writer, ",\"type\":") ||
        !confit_writer_json_string(writer, kind, strlen(kind)) ||
        !confit_writer_text(writer, ",\"value\":") ||
        !confit_writer_json_value(writer, &value->effective_value) ||
        !confit_writer_text(writer, ",\"default\":") ||
        !confit_writer_json_value(writer, &value->default_value) ||
        !confit_writer_text(writer, ",\"origin\":") ||
        !confit_writer_json_string(writer, origin, strlen(origin)) ||
        !confit_writer_text(writer, ",\"available\":") ||
        !confit_writer_text(writer, value->available ? "true}" : "false}"))
      return 0;
  }
  return confit_writer_text(writer, "]}\n");
}

static ConfitStatus confit_emitter_build_artifact(
    ConfitEmission *emission, ConfitEmitterKind kind, const char *role,
    const char *name, ConfitEmitterFormat format,
    const ConfitResolution *resolution, ConfitDiagnostic *diagnostic) {
  ConfitEmitterWriter counter;
  ConfitEmitterWriter writer;
  ConfitEmitterArtifact *artifact;
  size_t allocation_size;
  memset(&counter, 0, sizeof(counter));
  if (!format(resolution, &counter) ||
      counter.size > CONFIT_LIMIT_SNAPSHOT_BYTES || counter.size == SIZE_MAX)
    return confit_emitter_fail(diagnostic, CONFIT_ERR_VALIDATION, name,
                               kTooLarge);
  allocation_size = counter.size + 1U;
  artifact = &emission->artifacts[emission->artifact_count];
  artifact->bytes = (unsigned char *)emission->allocator.allocate(
      emission->allocator.context, allocation_size);
  if (artifact->bytes == 0)
    return confit_emitter_fail(diagnostic, CONFIT_ERR_INTERNAL, name,
                               kOutOfMemory);
  memset(&writer, 0, sizeof(writer));
  writer.bytes = artifact->bytes;
  writer.capacity = counter.size;
  if (!format(resolution, &writer) || writer.size != counter.size) {
    emission->allocator.deallocate(emission->allocator.context,
                                   artifact->bytes);
    memset(artifact, 0, sizeof(*artifact));
    return confit_emitter_fail(diagnostic, CONFIT_ERR_INTERNAL, name,
                               kInvalidResolution);
  }
  artifact->bytes[artifact->size = writer.size] = '\0';
  artifact->kind = kind;
  artifact->role = role;
  artifact->name = name;
  emission->artifact_count += 1U;
  return CONFIT_OK;
}

ConfitStatus confit_emit(const ConfitResolution *resolution,
                         const ConfitEmitRequest *request,
                         const ConfitAllocator *allocator,
                         ConfitEmission **out_emission,
                         ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitEmission *emission;
  ConfitStatus status;
  if (out_emission == 0)
    return confit_emitter_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                               kInvalidArgument);
  *out_emission = 0;
  if (resolution == 0 || !confit_emitter_request_is_valid(request))
    return confit_emitter_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                               kInvalidArgument);
  if (!confit_emitter_resolve_allocator(allocator, &resolved))
    return confit_emitter_fail(diagnostic, CONFIT_ERR_USAGE, 0,
                               kInvalidAllocator);
  status = confit_emitter_preflight(resolution, request, diagnostic);
  if (status != CONFIT_OK) return status;
  emission = (ConfitEmission *)resolved.allocate(resolved.context,
                                                 sizeof(*emission));
  if (emission == 0)
    return confit_emitter_fail(diagnostic, CONFIT_ERR_INTERNAL, 0,
                               kOutOfMemory);
  memset(emission, 0, sizeof(*emission));
  emission->allocator = resolved;
  if (request->emit_make) {
    status = confit_emitter_build_artifact(
        emission, CONFIT_EMITTER_MAKE, "make-values", "values.mk",
        confit_format_make, resolution, diagnostic);
    if (status != CONFIT_OK) goto fail;
  }
  if (request->emit_c_header) {
    status = confit_emitter_build_artifact(
        emission, CONFIT_EMITTER_C_HEADER, "c-header-values", "values.h",
        confit_format_c_header, resolution, diagnostic);
    if (status != CONFIT_OK) goto fail;
  }
  if (request->emit_json) {
    status = confit_emitter_build_artifact(
        emission, CONFIT_EMITTER_JSON, "resolved-values",
        "resolved-values.json", confit_format_json, resolution, diagnostic);
    if (status != CONFIT_OK) goto fail;
  }
  *out_emission = emission;
  return CONFIT_OK;

fail:
  confit_emission_destroy(emission);
  return status;
}

void confit_emission_destroy(ConfitEmission *emission) {
  size_t index;
  if (emission == 0) return;
  for (index = emission->artifact_count; index > 0U; --index) {
    ConfitEmitterArtifact *artifact = &emission->artifacts[index - 1U];
    if (artifact->bytes != 0)
      emission->allocator.deallocate(emission->allocator.context,
                                     artifact->bytes);
  }
  emission->allocator.deallocate(emission->allocator.context, emission);
}

size_t confit_emission_artifact_count(const ConfitEmission *emission) {
  return emission != 0 ? emission->artifact_count : 0U;
}

int confit_emission_artifact_at(const ConfitEmission *emission, size_t index,
                                ConfitEmittedArtifactView *out_view) {
  const ConfitEmitterArtifact *artifact;
  if (emission == 0 || out_view == 0 || index >= emission->artifact_count)
    return 0;
  artifact = &emission->artifacts[index];
  out_view->kind = artifact->kind;
  out_view->role = artifact->role;
  out_view->name = artifact->name;
  out_view->bytes = artifact->bytes;
  out_view->size = artifact->size;
  out_view->printable = 1;
  return 1;
}

int confit_emission_find_artifact(const ConfitEmission *emission,
                                  ConfitEmitterKind kind,
                                  ConfitEmittedArtifactView *out_view) {
  size_t index;
  if (emission == 0 || out_view == 0 || kind <= CONFIT_EMITTER_INVALID ||
      kind > CONFIT_EMITTER_JSON)
    return 0;
  for (index = 0U; index < emission->artifact_count; ++index) {
    if (emission->artifacts[index].kind == kind)
      return confit_emission_artifact_at(emission, index, out_view);
  }
  return 0;
}
