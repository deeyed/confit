#include "confit/generator_v2.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "confit/host.h"
#include "confit/build_policy.h"
#include "confit/version.h"

enum { CONFIT_V4_PUBLISHED_TEXT_COUNT = 16 };

typedef struct ConfitV2ArtifactBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitV2ArtifactBuilder;

static const char kHeaderListUnsupported[] =
    "schema v2 header artifact needs an explicit list encoding";

static char *confit_v4_strdup(const char *text) {
  const size_t size = text != 0 ? strlen(text) : 0U;
  char *copy = (char *)malloc(size + 1U);
  if (copy != 0) memcpy(copy, text, size + 1U);
  return copy;
}

static void confit_v2_builder_init(ConfitV2ArtifactBuilder *builder) {
  memset(builder, 0, sizeof(*builder));
}

static void confit_v2_builder_clear(ConfitV2ArtifactBuilder *builder) {
  free(builder->text);
  memset(builder, 0, sizeof(*builder));
}

static ConfitStatus confit_v2_builder_reserve(ConfitV2ArtifactBuilder *builder,
                                               size_t extra) {
  size_t required;
  size_t capacity;
  char *grown;

  if (extra > SIZE_MAX - builder->size - 1U) {
    return CONFIT_ERR_INTERNAL;
  }
  required = builder->size + extra + 1U;
  if (required <= builder->capacity) {
    return CONFIT_OK;
  }
  capacity = builder->capacity == 0U ? 1024U : builder->capacity;
  while (capacity < required) {
    if (capacity > SIZE_MAX / 2U) {
      capacity = required;
      break;
    }
    capacity *= 2U;
  }
  grown = (char *)realloc(builder->text, capacity);
  if (grown == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  builder->text = grown;
  builder->capacity = capacity;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_builder_append_n(ConfitV2ArtifactBuilder *builder,
                                                const char *text,
                                                size_t size) {
  ConfitStatus status = confit_v2_builder_reserve(builder, size);

  if (status != CONFIT_OK) {
    return status;
  }
  if (size > 0U) {
    memcpy(builder->text + builder->size, text, size);
  }
  builder->size += size;
  builder->text[builder->size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_v2_builder_append(ConfitV2ArtifactBuilder *builder,
                                              const char *text) {
  return confit_v2_builder_append_n(builder, text, strlen(text));
}

static ConfitStatus confit_v2_builder_append_char(ConfitV2ArtifactBuilder *builder,
                                                   char value) {
  return confit_v2_builder_append_n(builder, &value, 1U);
}

static ConfitStatus confit_v2_builder_appendf(ConfitV2ArtifactBuilder *builder,
                                               const char *format, ...) {
  va_list arguments;
  va_list copied;
  int written;
  ConfitStatus status;

  va_start(arguments, format);
  va_copy(copied, arguments);
  written = vsnprintf(0, 0U, format, copied);
  va_end(copied);
  if (written < 0) {
    va_end(arguments);
    return CONFIT_ERR_INTERNAL;
  }
  status = confit_v2_builder_reserve(builder, (size_t)written);
  if (status == CONFIT_OK) {
    (void)vsnprintf(builder->text + builder->size,
                    builder->capacity - builder->size, format, arguments);
    builder->size += (size_t)written;
  }
  va_end(arguments);
  return status;
}

static char *confit_v2_builder_take(ConfitV2ArtifactBuilder *builder) {
  char *text;

  if (builder->text == 0) {
    text = (char *)malloc(1U);
    if (text != 0) {
      text[0] = '\0';
    }
    return text;
  }
  text = builder->text;
  memset(builder, 0, sizeof(*builder));
  return text;
}

static int confit_v2_is_identifier_char(char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9');
}

static char confit_v2_to_upper(char value) {
  return value >= 'a' && value <= 'z' ? (char)(value - 'a' + 'A') : value;
}

static ConfitStatus confit_v2_append_json_string(ConfitV2ArtifactBuilder *builder,
                                                  const char *text) {
  size_t index;
  ConfitStatus status = confit_v2_builder_append_char(builder, '"');

  for (index = 0U; status == CONFIT_OK && text != 0 && text[index] != '\0';
       ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (value == '"' || value == '\\') {
      status = confit_v2_builder_append_char(builder, '\\');
      if (status == CONFIT_OK) {
        status = confit_v2_builder_append_char(builder, (char)value);
      }
    } else if (value == '\n') {
      status = confit_v2_builder_append(builder, "\\n");
    } else if (value == '\r') {
      status = confit_v2_builder_append(builder, "\\r");
    } else if (value == '\t') {
      status = confit_v2_builder_append(builder, "\\t");
    } else if (value < 0x20U) {
      status = confit_v2_builder_appendf(builder, "\\u%04X", (unsigned)value);
    } else {
      status = confit_v2_builder_append_char(builder, (char)value);
    }
  }
  if (status == CONFIT_OK) {
    status = confit_v2_builder_append_char(builder, '"');
  }
  return status;
}

static ConfitStatus confit_v2_append_c_string(ConfitV2ArtifactBuilder *builder,
                                               const char *text) {
  size_t index;
  ConfitStatus status = confit_v2_builder_append_char(builder, '"');

  for (index = 0U; status == CONFIT_OK && text != 0 && text[index] != '\0';
       ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (value == '"' || value == '\\') {
      status = confit_v2_builder_append_char(builder, '\\');
      if (status == CONFIT_OK) {
        status = confit_v2_builder_append_char(builder, (char)value);
      }
    } else if (value == '\n') {
      status = confit_v2_builder_append(builder, "\\n");
    } else if (value == '\r') {
      status = confit_v2_builder_append(builder, "\\r");
    } else if (value == '\t') {
      status = confit_v2_builder_append(builder, "\\t");
    } else if (value < 0x20U || value == 0x7FU) {
      status = confit_v2_builder_appendf(builder, "\\%03o", (unsigned)value);
    } else {
      status = confit_v2_builder_append_char(builder, (char)value);
    }
  }
  if (status == CONFIT_OK) {
    status = confit_v2_builder_append_char(builder, '"');
  }
  return status;
}

static ConfitStatus confit_v2_append_hex(ConfitV2ArtifactBuilder *builder,
                                         uint64_t value) {
  return confit_v2_builder_appendf(builder, "0x%016llX",
                                   (unsigned long long)value);
}

static ConfitStatus confit_v2_append_json_value(ConfitV2ArtifactBuilder *builder,
                                                 const ConfitV2Value *value,
                                                 ConfitV2OptionType type) {
  size_t index;

  if (value == 0 || value->kind == CONFIT_V2_VALUE_UNSET) {
    return confit_v2_builder_append(builder, "null");
  }
  switch (value->kind) {
  case CONFIT_V2_VALUE_BOOL:
    return confit_v2_builder_append(builder,
                                    value->as.bool_value ? "true" : "false");
  case CONFIT_V2_VALUE_TRISTATE:
    return confit_v2_append_json_string(builder,
                                        (char[2]){value->as.tristate_value, 0});
  case CONFIT_V2_VALUE_INT:
    return confit_v2_builder_appendf(builder, "%lld",
                                     (long long)value->as.int_value);
  case CONFIT_V2_VALUE_UINT:
    if (type == CONFIT_V2_OPTION_TYPE_HEX) {
      ConfitV2ArtifactBuilder text;
      ConfitStatus status;

      confit_v2_builder_init(&text);
      status = confit_v2_append_hex(&text, value->as.uint_value);
      if (status == CONFIT_OK) {
        status = confit_v2_append_json_string(builder, text.text);
      }
      confit_v2_builder_clear(&text);
      return status;
    }
    return confit_v2_builder_appendf(builder, "%llu",
                                     (unsigned long long)value->as.uint_value);
  case CONFIT_V2_VALUE_FLOAT:
    return confit_v2_builder_appendf(builder, "%.17g", value->as.float_value);
  case CONFIT_V2_VALUE_STRING:
    return confit_v2_append_json_string(builder, value->as.string_value);
  case CONFIT_V2_VALUE_STRING_LIST:
    if (confit_v2_builder_append_char(builder, '[') != CONFIT_OK) {
      return CONFIT_ERR_INTERNAL;
    }
    for (index = 0U; index < value->as.string_list.count; ++index) {
      ConfitStatus status;
      if (index != 0U &&
          (status = confit_v2_builder_append(builder, ", ")) != CONFIT_OK) {
        return status;
      }
      status = confit_v2_append_json_string(builder,
                                             value->as.string_list.items[index]);
      if (status != CONFIT_OK) {
        return status;
      }
    }
    return confit_v2_builder_append_char(builder, ']');
  case CONFIT_V2_VALUE_UNSET:
  default:
    return confit_v2_builder_append(builder, "null");
  }
}

static ConfitStatus confit_v2_append_macro_fragment(
    ConfitV2ArtifactBuilder *builder, const char *text) {
  size_t index;
  int separator = 1;

  for (index = 0U; text != 0 && text[index] != '\0'; ++index) {
    if (confit_v2_is_identifier_char(text[index])) {
      ConfitStatus status = confit_v2_builder_append_char(
          builder, confit_v2_to_upper(text[index]));
      if (status != CONFIT_OK) {
        return status;
      }
      separator = 0;
    } else if (!separator) {
      ConfitStatus status = confit_v2_builder_append_char(builder, '_');
      if (status != CONFIT_OK) {
        return status;
      }
      separator = 1;
    }
  }
  if (builder->size > 0U && builder->text[builder->size - 1U] == '_') {
    builder->size -= 1U;
    builder->text[builder->size] = '\0';
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_make_option_macro(const ConfitV2Snapshot *snapshot,
                                                 const char *id, char **out) {
  ConfitV2ArtifactBuilder builder;
  const char *project = confit_v2_snapshot_project_name(snapshot);
  const char *tail = id;
  size_t project_size = strlen(project);
  ConfitStatus status;

  if (strncmp(id, project, project_size) == 0 && id[project_size] == '.') {
    tail = id + project_size + 1U;
  }
  confit_v2_builder_init(&builder);
  status = confit_v2_append_macro_fragment(&builder, project);
  if (status == CONFIT_OK) {
    status = confit_v2_builder_append(&builder, "_CONFIG_");
  }
  if (status == CONFIT_OK) {
    status = confit_v2_append_macro_fragment(&builder, tail);
  }
  if (status == CONFIT_OK && builder.size == 0U) {
    status = CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) {
      status = CONFIT_ERR_INTERNAL;
    }
  }
  confit_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_v4_generate_header(const ConfitV2Snapshot *snapshot,
                                               char **out, ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactBuilder builder;
  char *guard = 0;
  size_t index;
  ConfitStatus status;

  confit_v2_builder_init(&builder);
  status = confit_v2_make_option_macro(snapshot, "header", &guard);
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, "#ifndef %s_H\n#define %s_H\n\n", guard, guard);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "#define CONFIT_SCHEMA_VERSION 2\n#define CONFIT_RESOLVER_ABI 2\n#define CONFIT_ARTIFACT_ABI 4\n");
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, "#define CONFIT_SOURCE_HASH \"0x%016llX\"\n#define CONFIT_INPUT_HASH \"0x%016llX\"\n#define CONFIT_SNAPSHOT_HASH \"0x%016llX\"\n\n#define CONFIT_TRISTATE_N 0\n#define CONFIT_TRISTATE_M 1\n#define CONFIT_TRISTATE_Y 2\n\n", (unsigned long long)confit_v2_snapshot_source_hash(snapshot), (unsigned long long)confit_v2_snapshot_input_hash(snapshot), (unsigned long long)confit_v2_snapshot_semantic_hash(snapshot));
  for (index = 0U; status == CONFIT_OK && index < confit_v2_snapshot_option_count(snapshot); ++index) {
    const ConfitV2SnapshotOption *option = confit_v2_snapshot_option_at(snapshot, index);
    char *macro = 0;
    if ((option->emit_mask & CONFIT_V2_EMIT_HEADER) == 0U) continue;
    status = confit_v2_make_option_macro(snapshot, option->id, &macro);
    if (status == CONFIT_OK && !option->effective_is_set) status = confit_v2_builder_appendf(&builder, "#define %s_SET 0\n", macro);
    if (status == CONFIT_OK && option->effective_is_set && option->effective_value.kind == CONFIT_V2_VALUE_STRING_LIST) status = CONFIT_ERR_UNSUPPORTED;
    if (status == CONFIT_OK && option->effective_is_set) {
      status = confit_v2_builder_appendf(&builder, "#define %s ", macro);
      if (status == CONFIT_OK) {
        switch (option->effective_value.kind) {
        case CONFIT_V2_VALUE_BOOL: status = confit_v2_builder_append(&builder, option->effective_value.as.bool_value ? "1" : "0"); break;
        case CONFIT_V2_VALUE_TRISTATE: status = confit_v2_builder_appendf(&builder, "CONFIT_TRISTATE_%c", option->effective_value.as.tristate_value == 'n' ? 'N' : option->effective_value.as.tristate_value == 'm' ? 'M' : 'Y'); break;
        case CONFIT_V2_VALUE_INT: status = confit_v2_builder_appendf(&builder, "%lldLL", (long long)option->effective_value.as.int_value); break;
        case CONFIT_V2_VALUE_UINT:
          if (option->type == CONFIT_V2_OPTION_TYPE_HEX) {
            status = confit_v2_append_hex(&builder,
                                          option->effective_value.as.uint_value);
          } else {
            status = confit_v2_builder_appendf(
                &builder, "%llu",
                (unsigned long long)option->effective_value.as.uint_value);
          }
          if (status == CONFIT_OK) {
            status = confit_v2_builder_append(&builder, "ULL");
          }
          break;
        case CONFIT_V2_VALUE_FLOAT: status = confit_v2_builder_appendf(&builder, "%a", option->effective_value.as.float_value); break;
        case CONFIT_V2_VALUE_STRING: status = confit_v2_append_c_string(&builder, option->effective_value.as.string_value); break;
        case CONFIT_V2_VALUE_STRING_LIST: status = CONFIT_ERR_UNSUPPORTED; break;
        case CONFIT_V2_VALUE_UNSET: default: status = CONFIT_ERR_INTERNAL; break;
        }
      }
      if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n");
    }
    free(macro);
  }
  if (status == CONFIT_ERR_UNSUPPORTED) confit_diagnostic_set(diagnostic, status, 0, 0, 0, kHeaderListUnsupported);
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, "\n#endif /* %s_H */\n", guard);
  if (status == CONFIT_OK) { *out = confit_v2_builder_take(&builder); if (*out == 0) status = CONFIT_ERR_INTERNAL; }
  free(guard);
  confit_v2_builder_clear(&builder);
  return status;
}

/* Removed: the v2 partial report, graph, explain, input and change emitters.
 * ABI v4 publishes exactly one sealed bundle, so leaving those renderers in
 * the binary would preserve a misleading alternate artifact authority. */
static int confit_v4_input_compare(const void *left, const void *right) {
  const ConfitV2ArtifactInput *const *left_input = left;
  const ConfitV2ArtifactInput *const *right_input = right;
  return strcmp((*left_input)->path, (*right_input)->path);
}

/* ABI v4 deliberately uses a self-contained SHA-256 implementation.  A host
 * OpenSSL/CommonCrypto dependency would make configuration identity depend on
 * an ambient library or its provider policy. */
typedef struct ConfitV4Sha256 {
  uint32_t state[8];
  uint64_t bit_count;
  unsigned char block[64];
  size_t block_size;
} ConfitV4Sha256;

static uint32_t confit_v4_rotr(uint32_t value, unsigned int shift) {
  return (value >> shift) | (value << (32U - shift));
}

static void confit_v4_sha256_init(ConfitV4Sha256 *hash) {
  static const uint32_t initial[] = {
      UINT32_C(0x6A09E667), UINT32_C(0xBB67AE85), UINT32_C(0x3C6EF372),
      UINT32_C(0xA54FF53A), UINT32_C(0x510E527F), UINT32_C(0x9B05688C),
      UINT32_C(0x1F83D9AB), UINT32_C(0x5BE0CD19)};

  memcpy(hash->state, initial, sizeof(initial));
  hash->bit_count = 0U;
  hash->block_size = 0U;
}

static void confit_v4_sha256_compress(ConfitV4Sha256 *hash,
                                      const unsigned char block[64]) {
  static const uint32_t constants[] = {
      UINT32_C(0x428A2F98), UINT32_C(0x71374491), UINT32_C(0xB5C0FBCF),
      UINT32_C(0xE9B5DBA5), UINT32_C(0x3956C25B), UINT32_C(0x59F111F1),
      UINT32_C(0x923F82A4), UINT32_C(0xAB1C5ED5), UINT32_C(0xD807AA98),
      UINT32_C(0x12835B01), UINT32_C(0x243185BE), UINT32_C(0x550C7DC3),
      UINT32_C(0x72BE5D74), UINT32_C(0x80DEB1FE), UINT32_C(0x9BDC06A7),
      UINT32_C(0xC19BF174), UINT32_C(0xE49B69C1), UINT32_C(0xEFBE4786),
      UINT32_C(0x0FC19DC6), UINT32_C(0x240CA1CC), UINT32_C(0x2DE92C6F),
      UINT32_C(0x4A7484AA), UINT32_C(0x5CB0A9DC), UINT32_C(0x76F988DA),
      UINT32_C(0x983E5152), UINT32_C(0xA831C66D), UINT32_C(0xB00327C8),
      UINT32_C(0xBF597FC7), UINT32_C(0xC6E00BF3), UINT32_C(0xD5A79147),
      UINT32_C(0x06CA6351), UINT32_C(0x14292967), UINT32_C(0x27B70A85),
      UINT32_C(0x2E1B2138), UINT32_C(0x4D2C6DFC), UINT32_C(0x53380D13),
      UINT32_C(0x650A7354), UINT32_C(0x766A0ABB), UINT32_C(0x81C2C92E),
      UINT32_C(0x92722C85), UINT32_C(0xA2BFE8A1), UINT32_C(0xA81A664B),
      UINT32_C(0xC24B8B70), UINT32_C(0xC76C51A3), UINT32_C(0xD192E819),
      UINT32_C(0xD6990624), UINT32_C(0xF40E3585), UINT32_C(0x106AA070),
      UINT32_C(0x19A4C116), UINT32_C(0x1E376C08), UINT32_C(0x2748774C),
      UINT32_C(0x34B0BCB5), UINT32_C(0x391C0CB3), UINT32_C(0x4ED8AA4A),
      UINT32_C(0x5B9CCA4F), UINT32_C(0x682E6FF3), UINT32_C(0x748F82EE),
      UINT32_C(0x78A5636F), UINT32_C(0x84C87814), UINT32_C(0x8CC70208),
      UINT32_C(0x90BEFFFA), UINT32_C(0xA4506CEB), UINT32_C(0xBEF9A3F7),
      UINT32_C(0xC67178F2)};
  uint32_t schedule[64];
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
  uint32_t f;
  uint32_t g;
  uint32_t h;
  size_t index;

  for (index = 0U; index < 16U; ++index) {
    const size_t offset = index * 4U;
    schedule[index] = ((uint32_t)block[offset] << 24U) |
                      ((uint32_t)block[offset + 1U] << 16U) |
                      ((uint32_t)block[offset + 2U] << 8U) |
                      (uint32_t)block[offset + 3U];
  }
  for (index = 16U; index < 64U; ++index) {
    const uint32_t small0 = confit_v4_rotr(schedule[index - 15U], 7U) ^
                            confit_v4_rotr(schedule[index - 15U], 18U) ^
                            (schedule[index - 15U] >> 3U);
    const uint32_t small1 = confit_v4_rotr(schedule[index - 2U], 17U) ^
                            confit_v4_rotr(schedule[index - 2U], 19U) ^
                            (schedule[index - 2U] >> 10U);
    schedule[index] = schedule[index - 16U] + small0 + schedule[index - 7U] +
                      small1;
  }
  a = hash->state[0]; b = hash->state[1]; c = hash->state[2]; d = hash->state[3];
  e = hash->state[4]; f = hash->state[5]; g = hash->state[6]; h = hash->state[7];
  for (index = 0U; index < 64U; ++index) {
    const uint32_t big1 = confit_v4_rotr(e, 6U) ^ confit_v4_rotr(e, 11U) ^
                          confit_v4_rotr(e, 25U);
    const uint32_t choose = (e & f) ^ ((~e) & g);
    const uint32_t temporary1 = h + big1 + choose + constants[index] + schedule[index];
    const uint32_t big0 = confit_v4_rotr(a, 2U) ^ confit_v4_rotr(a, 13U) ^
                          confit_v4_rotr(a, 22U);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temporary2 = big0 + majority;
    h = g; g = f; f = e; e = d + temporary1; d = c; c = b; b = a;
    a = temporary1 + temporary2;
  }
  hash->state[0] += a; hash->state[1] += b; hash->state[2] += c;
  hash->state[3] += d; hash->state[4] += e; hash->state[5] += f;
  hash->state[6] += g; hash->state[7] += h;
}

static void confit_v4_sha256_update(ConfitV4Sha256 *hash,
                                    const unsigned char *text, size_t size) {
  size_t index = 0U;

  hash->bit_count += (uint64_t)size * UINT64_C(8);
  while (index < size) {
    const size_t available = 64U - hash->block_size;
    const size_t remaining = size - index;
    const size_t copy_size = remaining < available ? remaining : available;
    memcpy(hash->block + hash->block_size, text + index, copy_size);
    hash->block_size += copy_size;
    index += copy_size;
    if (hash->block_size == sizeof(hash->block)) {
      confit_v4_sha256_compress(hash, hash->block);
      hash->block_size = 0U;
    }
  }
}

static void confit_v4_sha256_final(ConfitV4Sha256 *hash,
                                   unsigned char output[32]) {
  size_t index;
  const uint64_t bit_count = hash->bit_count;

  hash->block[hash->block_size++] = 0x80U;
  if (hash->block_size > 56U) {
    while (hash->block_size < 64U) hash->block[hash->block_size++] = 0U;
    confit_v4_sha256_compress(hash, hash->block);
    hash->block_size = 0U;
  }
  while (hash->block_size < 56U) hash->block[hash->block_size++] = 0U;
  for (index = 0U; index < 8U; ++index) {
    hash->block[63U - index] = (unsigned char)(bit_count >> (index * 8U));
  }
  confit_v4_sha256_compress(hash, hash->block);
  for (index = 0U; index < 8U; ++index) {
    output[index * 4U] = (unsigned char)(hash->state[index] >> 24U);
    output[index * 4U + 1U] = (unsigned char)(hash->state[index] >> 16U);
    output[index * 4U + 2U] = (unsigned char)(hash->state[index] >> 8U);
    output[index * 4U + 3U] = (unsigned char)hash->state[index];
  }
}

static void confit_v4_sha256_text(const char *text, char output[65]) {
  confit_v4_sha256_bytes(text, strlen(text), output);
}

void confit_v4_sha256_bytes(const void *data, size_t size, char output[65]) {
  static const char digits[] = "0123456789abcdef";
  unsigned char bytes[32];
  ConfitV4Sha256 hash;
  size_t index;

  if (output == 0) return;
  if (data == 0 && size != 0U) {
    output[0] = '\0';
    return;
  }
  confit_v4_sha256_init(&hash);
  if (size != 0U) {
    confit_v4_sha256_update(&hash, (const unsigned char *)data, size);
  }
  confit_v4_sha256_final(&hash, bytes);
  for (index = 0U; index < sizeof(bytes); ++index) {
    output[index * 2U] = digits[bytes[index] >> 4U];
    output[index * 2U + 1U] = digits[bytes[index] & 0x0FU];
  }
  output[64] = '\0';
}

void confit_v4_sha256_hex(const char *text, char output[65]) {
  if (output == 0) return;
  if (text == 0) {
    output[0] = '\0';
    return;
  }
  confit_v4_sha256_text(text, output);
}

ConfitStatus confit_v4_sha256_file(const char *path, char output[65],
                                   ConfitDiagnostic *diagnostic) {
  static const char digits[] = "0123456789abcdef";
  unsigned char buffer[16384];
  unsigned char bytes[32];
  ConfitV4Sha256 hash;
  FILE *file;
  size_t total = 0U;
  size_t index;

  if (path == 0 || path[0] != '/' || output == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, path, 0U,
                          0U, "invalid executable digest input");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  file = fopen(path, "rb");
  if (file == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, path, 0U, 0U,
                          "sealed executable cannot be opened for digest");
    return CONFIT_ERR_UNSUPPORTED;
  }
  confit_v4_sha256_init(&hash);
  for (;;) {
    const size_t count = fread(buffer, 1U, sizeof(buffer), file);
    if (count != 0U) {
      if (count > (256U * 1024U * 1024U) - total) {
        (void)fclose(file);
        output[0] = '\0';
        confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, path, 0U, 0U,
                              "sealed executable exceeds 256 MiB digest bound");
        return CONFIT_ERR_GENERATION;
      }
      total += count;
      confit_v4_sha256_update(&hash, buffer, count);
    }
    if (count < sizeof(buffer)) {
      if (ferror(file)) {
        (void)fclose(file);
        output[0] = '\0';
        confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, path, 0U, 0U,
                              "sealed executable digest read failed");
        return CONFIT_ERR_UNSUPPORTED;
      }
      break;
    }
  }
  if (fclose(file) != 0 || total == 0U) {
    output[0] = '\0';
    confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, path, 0U, 0U,
                          "sealed executable digest input is empty or unstable");
    return CONFIT_ERR_UNSUPPORTED;
  }
  confit_v4_sha256_final(&hash, bytes);
  for (index = 0U; index < sizeof(bytes); ++index) {
    output[index * 2U] = digits[bytes[index] >> 4U];
    output[index * 2U + 1U] = digits[bytes[index] & 0x0FU];
  }
  output[64] = '\0';
  return CONFIT_OK;
}

static int confit_v4_is_sha256(const char *text) {
  size_t index;
  if (text == 0 || strncmp(text, "sha256:", 7U) != 0 || strlen(text) != 71U) {
    return 0;
  }
  for (index = 7U; index < 71U; ++index) {
    if (!((text[index] >= '0' && text[index] <= '9') ||
          (text[index] >= 'a' && text[index] <= 'f'))) return 0;
  }
  return 1;
}

static int confit_v4_is_raw_sha256(const char *text) {
  size_t index;
  if (text == 0 || strlen(text) != 64U) return 0;
  for (index = 0U; index < 64U; ++index) {
    if (!((text[index] >= '0' && text[index] <= '9') ||
          (text[index] >= 'a' && text[index] <= 'f'))) return 0;
  }
  return 1;
}

static int confit_v4_is_safe_atom(const char *text, int path) {
  size_t index;
  size_t segment_start = 0U;

  if (text == 0 || text[0] == '\0' || (path && text[0] == '/')) return 0;
  for (index = 0U; text[index] != '\0'; ++index) {
    const char value = text[index];
    const int allowed = (value >= 'a' && value <= 'z') ||
                        (value >= 'A' && value <= 'Z') ||
                        (value >= '0' && value <= '9') || value == '_' ||
                        value == '-' || value == '.' || value == '+' ||
                        value == '@' ||
                        (path && value == '/');
    if (!allowed) return 0;
    if (path && (value == '/' || text[index + 1U] == '\0')) {
      const size_t segment_end = value == '/' ? index : index + 1U;
      const size_t segment_size = segment_end - segment_start;
      if (segment_size == 0U ||
          (segment_size == 1U && text[segment_start] == '.') ||
          (segment_size == 2U && text[segment_start] == '.' &&
           text[segment_start + 1U] == '.')) return 0;
      segment_start = index + 1U;
    }
  }
  return 1;
}

static const char *confit_v4_option_type_name(ConfitV2OptionType type) {
  switch (type) {
  case CONFIT_V2_OPTION_TYPE_BOOL:
    return "bool";
  case CONFIT_V2_OPTION_TYPE_TRISTATE:
    return "tristate";
  case CONFIT_V2_OPTION_TYPE_INT:
    return "int";
  case CONFIT_V2_OPTION_TYPE_UINT:
    return "uint";
  case CONFIT_V2_OPTION_TYPE_HEX:
    return "hex";
  case CONFIT_V2_OPTION_TYPE_FLOAT:
    return "float";
  case CONFIT_V2_OPTION_TYPE_STRING:
    return "string";
  case CONFIT_V2_OPTION_TYPE_ENUM:
    return "enum";
  case CONFIT_V2_OPTION_TYPE_PATH:
    return "path";
  case CONFIT_V2_OPTION_TYPE_STRING_LIST:
    return "string_list";
  case CONFIT_V2_OPTION_TYPE_PATH_LIST:
    return "path_list";
  case CONFIT_V2_OPTION_TYPE_ENUM_SET:
    return "enum_set";
  case CONFIT_V2_OPTION_TYPE_INVALID:
  default:
    return "invalid";
  }
}

static ConfitStatus confit_v4_append_identity(
    ConfitV2ArtifactBuilder *builder, const ConfitV2Snapshot *snapshot,
    const char *schema, const char *tool_identity) {
  ConfitStatus status = confit_v2_builder_append(builder, "  \"schema\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(builder, schema);
  if (status == CONFIT_OK) status = confit_v2_builder_append(
      builder, ",\n  \"artifact_abi\": 4,\n  \"resolver_abi\": \"confit-resolver-v2\",\n  \"tool\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(builder, tool_identity);
  if (status == CONFIT_OK) status = confit_v2_builder_append(builder, ",\n  \"project\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(builder, confit_v2_snapshot_project_name(snapshot));
  if (status == CONFIT_OK) status = confit_v2_builder_append(builder, ",\n  \"profile\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(builder, confit_v2_snapshot_profile_name(snapshot));
  if (status == CONFIT_OK) status = confit_v2_builder_append(builder, ",\n  \"target\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(builder, confit_v2_snapshot_target_name(snapshot));
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(
      builder, ",\n  \"snapshot_fast_hint\": \"fnv1a64:%016llx\"",
      (unsigned long long)confit_v2_snapshot_semantic_hash(snapshot));
  return status;
}

static ConfitStatus confit_v4_append_component_id_array(
    ConfitV2ArtifactBuilder *builder, const ConfitComponentClosure *closure) {
  ConfitStatus status = confit_v2_builder_append(builder, "[");
  size_t index;
  for (index = 0U; status == CONFIT_OK && closure != 0 && index < closure->component_count;
       ++index) {
    if (index != 0U) status = confit_v2_builder_append(builder, ", ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(
        builder, closure->ordered[index]->id);
  }
  return status == CONFIT_OK ? confit_v2_builder_append(builder, "]") : status;
}

static ConfitStatus confit_v4_append_component_root_array(
    ConfitV2ArtifactBuilder *builder, const ConfitComponentClosure *closure) {
  ConfitStatus status = confit_v2_builder_append(builder, "[");
  size_t index;
  for (index = 0U; status == CONFIT_OK && closure != 0 &&
                        index < closure->root_feature_count;
       ++index) {
    if (index != 0U) status = confit_v2_builder_append(builder, ", ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(
        builder, closure->root_features[index]);
  }
  return status == CONFIT_OK ? confit_v2_builder_append(builder, "]") : status;
}

static ConfitStatus confit_v4_append_component_atom_array(
    ConfitV2ArtifactBuilder *builder, char *const *items, size_t count);

static ConfitStatus confit_v4_generate_selection(
    const ConfitV2Snapshot *snapshot, const char *tool_identity,
    const ConfitComponentCatalog *catalog, const ConfitComponentClosure *closure,
    const ConfitV4ArtifactOptions *options, const char *catalog_digest,
    char **out) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  size_t index;
  const char *profile_digest = 0;

  for (index = 0U; options != 0 && index < options->input_count; ++index) {
    if (strcmp(options->inputs[index].role, "profile") == 0) {
      profile_digest = options->inputs[index].content_hash;
      break;
    }
  }

  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n");
  if (status == CONFIT_OK) status = confit_v4_append_identity(
      &builder, snapshot, "confit-selection-v4", tool_identity);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ",\n  \"options\": [\n");
  for (index = 0U; status == CONFIT_OK &&
                    index < confit_v2_snapshot_option_count(snapshot); ++index) {
    const ConfitV2SnapshotOption *option = confit_v2_snapshot_option_at(snapshot, index);
    status = confit_v2_builder_append(&builder, "    { \"id\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, option->id);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"type\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(
        &builder, confit_v4_option_type_name(option->type));
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"effective\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_value(
        &builder, option->effective_is_set ? &option->effective_value : 0,
        option->type);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, ", \"available\": %s, \"visible\": %s }%s\n",
        option->available ? "true" : "false", option->visible ? "true" : "false",
        index + 1U == confit_v2_snapshot_option_count(snapshot) ? "" : ",");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(
      &builder,
      "  ],\n  \"components\": { \"schema_version\": 3, "
      "\"selection_authority\": \"build-time-only\", \"catalog_state\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(
      &builder, catalog != 0 ? "available" : "unavailable");
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"root_features\": ");
  if (status == CONFIT_OK) status = confit_v4_append_component_root_array(&builder, closure);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"selected\": ");
  if (status == CONFIT_OK) status = confit_v4_append_component_id_array(&builder, closure);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"catalog_digest\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(
      &builder, catalog_digest != 0 ? catalog_digest : "unavailable");
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"profile_digest\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(
      &builder, profile_digest != 0 ? profile_digest : "unavailable");
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"kapi_requires\": ");
  if (status == CONFIT_OK) status = confit_v4_append_component_atom_array(
      &builder, closure != 0 ? closure->kapi_requires : 0,
      closure != 0 ? closure->kapi_requirement_count : 0U);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"kapi_provides\": ");
  if (status == CONFIT_OK) status = confit_v4_append_component_atom_array(
      &builder, closure != 0 ? closure->kapi_provides : 0,
      closure != 0 ? closure->kapi_provide_count : 0U);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, " }\n}\n");
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_v4_generate_report(
    const ConfitV2Snapshot *snapshot, const char *tool_identity,
    const ConfitComponentCatalog *catalog, const ConfitComponentClosure *closure,
    char **out) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;

  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n");
  if (status == CONFIT_OK) status = confit_v4_append_identity(
      &builder, snapshot, "confit-report-v4", tool_identity);
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(
      &builder, ",\n  \"option_count\": %llu,\n  \"component_catalog\": \"%s\",\n  \"component_count\": %llu,\n  \"selected_component_count\": %llu\n}\n",
      (unsigned long long)confit_v2_snapshot_option_count(snapshot),
      catalog != 0 ? "available" : "unavailable",
      (unsigned long long)(catalog != 0 ? catalog->component_count : 0U),
      (unsigned long long)(closure != 0 ? closure->component_count : 0U));
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_v4_generate_inputs(
    const ConfitV2Snapshot *snapshot, const ConfitV4ArtifactOptions *options,
    const char *tool_identity, char **out, ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactBuilder builder;
  const ConfitV2ArtifactInput **ordered = 0;
  ConfitStatus status = CONFIT_OK;
  size_t index;

  if (options->input_count > 0U && options->inputs == 0) return CONFIT_ERR_INVALID_ARGUMENT;
  if (options->input_count > 0U) {
    ordered = calloc(options->input_count, sizeof(*ordered));
    if (ordered == 0) return CONFIT_ERR_INTERNAL;
    for (index = 0U; index < options->input_count; ++index) ordered[index] = &options->inputs[index];
    qsort(ordered, options->input_count, sizeof(*ordered), confit_v4_input_compare);
  }
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n");
  if (status == CONFIT_OK) status = confit_v4_append_identity(
      &builder, snapshot, "confit-inputs-v4", tool_identity);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ",\n  \"inputs\": [\n");
  for (index = 0U; status == CONFIT_OK && index < options->input_count; ++index) {
    const ConfitV2ArtifactInput *input = ordered[index];
    if (input->path == 0 || input->role == 0 || !confit_v4_is_sha256(input->content_hash) ||
        !confit_v4_is_safe_atom(input->path, 1) || !confit_v4_is_safe_atom(input->role, 0) ||
        (index > 0U && strcmp(ordered[index - 1U]->path, input->path) == 0)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                            input != 0 ? input->path : 0, 0U, 0U,
                            "invalid or duplicate sealed provenance input");
      status = CONFIT_ERR_INVALID_ARGUMENT;
      break;
    }
    status = confit_v2_builder_append(&builder, "    { \"path\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, input->path);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"sha256\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, input->content_hash);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"role\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, input->role);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, " }%s\n", index + 1U == options->input_count ? "" : ",");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  ]\n}\n");
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_clear(&builder);
  free(ordered);
  return status;
}

static ConfitStatus confit_v4_append_make_value(
    ConfitV2ArtifactBuilder *builder, const ConfitV2SnapshotOption *option,
    ConfitDiagnostic *diagnostic) {
  const ConfitV2Value *value = &option->effective_value;
  size_t index;
  ConfitStatus status;

  if (!option->effective_is_set) return confit_v2_builder_append(builder, "unset");
  switch (value->kind) {
  case CONFIT_V2_VALUE_BOOL:
    return confit_v2_builder_append(builder, value->as.bool_value ? "true" : "false");
  case CONFIT_V2_VALUE_TRISTATE:
    return confit_v2_builder_append_char(builder, value->as.tristate_value);
  case CONFIT_V2_VALUE_INT:
    return confit_v2_builder_appendf(builder, "%lld", (long long)value->as.int_value);
  case CONFIT_V2_VALUE_UINT:
    return confit_v2_builder_appendf(builder, "%llu", (unsigned long long)value->as.uint_value);
  case CONFIT_V2_VALUE_FLOAT:
    return confit_v2_builder_appendf(builder, "%.17g", value->as.float_value);
  case CONFIT_V2_VALUE_STRING:
    if (!confit_v4_is_safe_atom(value->as.string_value,
                                option->type == CONFIT_V2_OPTION_TYPE_PATH)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0U, 0U,
                            "unsafe value cannot enter generated Make syntax");
      return CONFIT_ERR_SCHEMA;
    }
    return confit_v2_builder_append(builder, value->as.string_value);
  case CONFIT_V2_VALUE_STRING_LIST:
    for (index = 0U; index < value->as.string_list.count; ++index) {
      if (!confit_v4_is_safe_atom(value->as.string_list.items[index],
                                  option->type == CONFIT_V2_OPTION_TYPE_PATH_LIST)) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0U, 0U,
                              "unsafe list value cannot enter generated Make syntax");
        return CONFIT_ERR_SCHEMA;
      }
      if (index != 0U && (status = confit_v2_builder_append_char(builder, ' ')) != CONFIT_OK) return status;
      status = confit_v2_builder_append(builder, value->as.string_list.items[index]);
      if (status != CONFIT_OK) return status;
    }
    return CONFIT_OK;
  case CONFIT_V2_VALUE_UNSET:
  default:
    return CONFIT_ERR_SCHEMA;
  }
}

/* A header-emitted value is not automatically safe Make data.  The header is
 * allowed to represent strings such as an ISA name with ':'; the adapter must
 * omit those values rather than either escaping them into Make syntax or
 * rejecting an otherwise valid, tool-neutral generation. */
static int confit_v4_make_value_is_safe(const ConfitV2SnapshotOption *option) {
  const ConfitV2Value *value;
  size_t index;

  if (option == 0) return 0;
  value = &option->effective_value;
  switch (value->kind) {
  case CONFIT_V2_VALUE_BOOL:
  case CONFIT_V2_VALUE_TRISTATE:
  case CONFIT_V2_VALUE_INT:
  case CONFIT_V2_VALUE_UINT:
  case CONFIT_V2_VALUE_FLOAT:
    return 1;
  case CONFIT_V2_VALUE_STRING:
    return confit_v4_is_safe_atom(
        value->as.string_value, option->type == CONFIT_V2_OPTION_TYPE_PATH);
  case CONFIT_V2_VALUE_STRING_LIST:
    for (index = 0U; index < value->as.string_list.count; ++index) {
      if (!confit_v4_is_safe_atom(
              value->as.string_list.items[index],
              option->type == CONFIT_V2_OPTION_TYPE_PATH_LIST)) {
        return 0;
      }
    }
    return 1;
  case CONFIT_V2_VALUE_UNSET:
  default:
    return 0;
  }
}

static ConfitStatus confit_v4_generate_make_values(
    const ConfitV2Snapshot *snapshot, char **out, ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  size_t index;

  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder,
      "# Generated by Confit; literal assignments only.\n");
  for (index = 0U; status == CONFIT_OK &&
                    index < confit_v2_snapshot_option_count(snapshot); ++index) {
    const ConfitV2SnapshotOption *option = confit_v2_snapshot_option_at(snapshot, index);
    char *macro = 0;
    if ((option->emit_mask & CONFIT_V2_EMIT_HEADER) == 0U) continue;
    if (!confit_v4_make_value_is_safe(option)) continue;
    status = confit_v2_make_option_macro(snapshot, option->id, &macro);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, "%s:= ", macro);
    if (status == CONFIT_OK) status = confit_v4_append_make_value(&builder, option, diagnostic);
    if (status == CONFIT_OK) status = confit_v2_builder_append_char(&builder, '\n');
    free(macro);
  }
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_v4_component_make_identifier(
    const char *id, char *out, size_t out_size) {
  size_t index;
  if (id == 0 || strlen(id) + 1U > out_size) return CONFIT_ERR_INVALID_ARGUMENT;
  for (index = 0U; id[index] != '\0'; ++index) {
    out[index] = id[index] == '.' ? '_' : id[index];
  }
  out[index] = '\0';
  return CONFIT_OK;
}

static int confit_v4_list_has(char *const *items, size_t count,
                              const char *value) {
  size_t index;
  for (index = 0U; index < count; ++index)
    if (strcmp(items[index], value) == 0) return 1;
  return 0;
}

static ConfitStatus confit_v4_resolve_link_owner(
    const ConfitComponentClosure *closure,
    const ConfitNucleusCatalog *nucleus, const char *consumer,
    const char *link, const char **out, ConfitDiagnostic *diagnostic) {
  size_t index;
  size_t matches = 0U;
  const char *resolved = NULL;
  if (closure == NULL || nucleus == NULL || consumer == NULL || link == NULL ||
      out == NULL) return CONFIT_ERR_INVALID_ARGUMENT;
  if (strchr(link, '@') != NULL) {
    for (index = 0U; index < closure->component_count; ++index) {
      const ConfitComponent *candidate = closure->ordered[index];
      if (confit_v4_list_has(candidate->feature_provides,
                             candidate->feature_provide_count, link)) {
        resolved = candidate->id;
        ++matches;
      }
    }
  } else {
    for (index = 0U; index < closure->component_count; ++index) {
      if (strcmp(closure->ordered[index]->id, link) == 0) {
        resolved = link;
        ++matches;
      }
    }
    for (index = 0U; index < nucleus->unit_count; ++index) {
      if (strcmp(nucleus->units[index].id, link) == 0) {
        resolved = link;
        ++matches;
      }
    }
  }
  if (matches != 1U || resolved == NULL || strcmp(consumer, resolved) == 0) {
    confit_diagnostic_set(
        diagnostic, CONFIT_ERR_SCHEMA, link, 0U, 0U,
        "LINK_USES must resolve to one different selected source owner");
    return CONFIT_ERR_SCHEMA;
  }
  *out = resolved;
  return CONFIT_OK;
}

static ConfitStatus confit_v4_kapi_facade_directory(
    const ConfitComponentClosure *closure,
    const ConfitNucleusCatalog *nucleus, const char *kapi,
    char *out, size_t out_size, ConfitDiagnostic *diagnostic) {
  const char *directory = NULL;
  size_t directory_size = 0U;
  size_t providers = 0U;
  size_t index;
  for (index = 0U; closure != NULL && index < closure->component_count;
       ++index) {
    const ConfitComponent *component = closure->ordered[index];
    if (confit_v4_list_has(component->kapi_provides,
                           component->kapi_provide_count, kapi)) {
      const char *separator = strrchr(component->makefile_path, '/');
      if (separator == NULL) return CONFIT_ERR_SCHEMA;
      directory = component->makefile_path;
      directory_size = (size_t)(separator - component->makefile_path);
      ++providers;
    }
  }
  for (index = 0U; nucleus != NULL && index < nucleus->unit_count; ++index) {
    const ConfitNucleusUnit *unit = &nucleus->units[index];
    if (confit_v4_list_has(unit->kapi_exports, unit->kapi_export_count, kapi)) {
      directory = unit->directory;
      directory_size = strlen(unit->directory);
      ++providers;
    }
  }
  if (providers != 1U || directory == NULL || directory_size == 0U ||
      directory_size + sizeof("/kapi/include") > out_size) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, kapi, 0U, 0U,
                          "KAPI provider has no exact bounded facade directory");
    return CONFIT_ERR_SCHEMA;
  }
  memcpy(out, directory, directory_size);
  memcpy(out + directory_size, "/kapi/include", sizeof("/kapi/include"));
  if (!confit_v4_is_safe_atom(out, 1)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, kapi, 0U, 0U,
                          "unsafe KAPI facade path cannot enter generated Make syntax");
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v4_generate_components_mk(
    const ConfitComponentCatalog *catalog, const ConfitComponentClosure *closure,
    const ConfitNucleusCatalog *nucleus, char **out,
    ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  size_t index;
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder,
      "# Generated by Confit; selected component closure only.\nPARUS_COMPONENT_IDS:=");
  for (index = 0U; status == CONFIT_OK && closure != 0 && index < closure->component_count;
       ++index) {
    if (!confit_v4_is_safe_atom(closure->ordered[index]->id, 0)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, closure->ordered[index]->id,
                            0U, 0U, "unsafe component ID cannot enter generated Make syntax");
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    status = confit_v2_builder_append(&builder, " ");
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, closure->ordered[index]->id);
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\nPARUS_COMPONENT_ORDER:=");
  for (index = 0U; status == CONFIT_OK && closure != 0 && index < closure->component_count;
       ++index) {
    status = confit_v2_builder_append(&builder, " ");
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, closure->ordered[index]->id);
  }
  for (index = 0U; status == CONFIT_OK && closure != 0 && index < closure->component_count;
       ++index) {
    const ConfitComponent *component = closure->ordered[index];
    char identifier[256];
    char source_directory[1024];
    const char *separator;
    size_t source_directory_size;
    size_t source_index;
    status = confit_v4_component_make_identifier(component->id, identifier,
                                                  sizeof(identifier));
    separator = strrchr(component->makefile_path, '/');
    source_directory_size = separator != 0
                                ? (size_t)(separator - component->makefile_path)
                                : 0U;
    if (status == CONFIT_OK &&
        (separator == 0 || source_directory_size == 0U ||
         source_directory_size + 1U > sizeof(source_directory))) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->id, 0U,
                            0U, "component source directory cannot enter generated Make syntax");
      status = CONFIT_ERR_SCHEMA;
    }
    if (status == CONFIT_OK) {
      memcpy(source_directory, component->makefile_path, source_directory_size);
      source_directory[source_directory_size] = '\0';
    }
    if (status == CONFIT_OK &&
        (!confit_v4_is_safe_atom(component->manifest_path, 1) ||
         !confit_v4_is_safe_atom(component->makefile_path, 1) ||
         !confit_v4_is_safe_atom(component->build_include, 1) ||
         !confit_v4_is_safe_atom(source_directory, 1))) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->id, 0U, 0U,
                            "unsafe component path cannot enter generated Make syntax");
      status = CONFIT_ERR_SCHEMA;
    }
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder,
        "\nPARUS_COMPONENT_%s_MANIFEST:= %s"
        "\nPARUS_COMPONENT_%s_MAKEFILE:= %s"
        "\nPARUS_COMPONENT_%s_BUILD_INCLUDE:= %s"
        "\nPARUS_COMPONENT_%s_SOURCE_DIR:= %s"
        "\nPARUS_COMPONENT_%s_KIND:= %s"
        "\nPARUS_COMPONENT_%s_FEATURE_REQUIRES:=",
        identifier, component->manifest_path, identifier,
        component->makefile_path, identifier, component->build_include,
        identifier, source_directory, identifier,
        confit_component_kind_name(component->kind), identifier);
    for (source_index = 0U; status == CONFIT_OK &&
         source_index < component->feature_requirement_count; ++source_index) {
      if (!confit_v4_is_safe_atom(component->feature_requires[source_index], 0)) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->id, 0U,
                              0U, "unsafe feature requirement cannot enter generated Make syntax");
        status = CONFIT_ERR_SCHEMA;
      } else {
        status = confit_v2_builder_appendf(
            &builder, " %s", component->feature_requires[source_index]);
      }
    }
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, "\nPARUS_COMPONENT_%s_FEATURE_PROVIDES:=", identifier);
    for (source_index = 0U; status == CONFIT_OK &&
         source_index < component->feature_provide_count; ++source_index) {
      if (!confit_v4_is_safe_atom(component->feature_provides[source_index], 0)) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->id, 0U,
                              0U, "unsafe feature provider cannot enter generated Make syntax");
        status = CONFIT_ERR_SCHEMA;
      } else {
        status = confit_v2_builder_appendf(
            &builder, " %s", component->feature_provides[source_index]);
      }
    }
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, "\nPARUS_COMPONENT_%s_FEATURE_CONFLICTS:=", identifier);
    for (source_index = 0U; status == CONFIT_OK &&
         source_index < component->feature_conflict_count; ++source_index) {
      if (!confit_v4_is_safe_atom(component->feature_conflicts[source_index], 0)) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->id, 0U,
                              0U, "unsafe feature conflict cannot enter generated Make syntax");
        status = CONFIT_ERR_SCHEMA;
      } else {
        status = confit_v2_builder_appendf(
            &builder, " %s", component->feature_conflicts[source_index]);
      }
    }
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, "\nPARUS_COMPONENT_%s_KAPI_REQUIRES:=", identifier);
    for (source_index = 0U; status == CONFIT_OK &&
         source_index < component->kapi_requirement_count; ++source_index) {
      if (!confit_v4_is_safe_atom(component->kapi_requires[source_index], 0)) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->id, 0U,
                              0U, "unsafe KAPI requirement cannot enter generated Make syntax");
        status = CONFIT_ERR_SCHEMA;
      } else {
        status = confit_v2_builder_appendf(
            &builder, " %s", component->kapi_requires[source_index]);
      }
    }
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, "\nPARUS_COMPONENT_%s_KAPI_INCLUDE_ROOTS:=", identifier);
    for (source_index = 0U; status == CONFIT_OK &&
         source_index < component->kapi_requirement_count; ++source_index) {
      char provider_directory[1024];
      status = confit_v4_kapi_facade_directory(
          closure, nucleus, component->kapi_requires[source_index],
          provider_directory, sizeof(provider_directory), diagnostic);
      if (status == CONFIT_OK)
        status = confit_v2_builder_appendf(&builder, " %s",
                                           provider_directory);
    }
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, "\nPARUS_COMPONENT_%s_KAPI_PROVIDES:=", identifier);
    for (source_index = 0U; status == CONFIT_OK &&
         source_index < component->kapi_provide_count; ++source_index) {
      if (!confit_v4_is_safe_atom(component->kapi_provides[source_index], 0)) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->id, 0U,
                              0U, "unsafe KAPI provider cannot enter generated Make syntax");
        status = CONFIT_ERR_SCHEMA;
      } else {
        status = confit_v2_builder_appendf(
            &builder, " %s", component->kapi_provides[source_index]);
      }
    }
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, "\nPARUS_COMPONENT_%s_LINK_USES:=", identifier);
    for (source_index = 0U; status == CONFIT_OK &&
         source_index < component->link_use_count; ++source_index) {
      const char *resolved = NULL;
      status = confit_v4_resolve_link_owner(
          closure, nucleus, component->id, component->link_uses[source_index],
          &resolved, diagnostic);
      if (status == CONFIT_OK && confit_v4_is_safe_atom(resolved, 0)) {
        status = confit_v2_builder_appendf(
            &builder, " %s", resolved);
      } else if (status == CONFIT_OK) {
        status = CONFIT_ERR_SCHEMA;
      }
    }
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, "\nPARUS_COMPONENT_%s_SRCS:=", identifier);
    for (source_index = 0U; status == CONFIT_OK &&
         source_index < component->source_count; ++source_index) {
      if (!confit_v4_is_safe_atom(component->sources[source_index], 1)) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->id, 0U,
                              0U, "unsafe component source cannot enter generated Make syntax");
        status = CONFIT_ERR_SCHEMA;
      } else {
        status = confit_v2_builder_appendf(
            &builder, " %s", component->sources[source_index]);
      }
    }
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, "\nPARUS_COMPONENT_%s_PUBLIC_HEADERS:=", identifier);
    for (source_index = 0U; status == CONFIT_OK &&
         source_index < component->public_header_count; ++source_index) {
      if (!confit_v4_is_safe_atom(component->public_headers[source_index], 1)) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->id, 0U,
                              0U, "unsafe public header cannot enter generated Make syntax");
        status = CONFIT_ERR_SCHEMA;
      } else {
        status = confit_v2_builder_appendf(
            &builder, " %s", component->public_headers[source_index]);
      }
    }
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n");
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_clear(&builder);
  (void)catalog;
  return status;
}

static ConfitStatus confit_v4_append_component_atom_array(
    ConfitV2ArtifactBuilder *builder, char *const *items, size_t count) {
  ConfitStatus status = confit_v2_builder_append(builder, "[");
  size_t index;
  for (index = 0U; status == CONFIT_OK && index < count; ++index) {
    if (index != 0U) status = confit_v2_builder_append(builder, ", ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(builder, items[index]);
  }
  return status == CONFIT_OK ? confit_v2_builder_append(builder, "]") : status;
}

static ConfitStatus confit_v4_generate_component_catalog_json(
    const ConfitComponentCatalog *catalog, const ConfitComponentClosure *closure,
    char **out) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  size_t index;
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n  \"schema\": \"confit-component-catalog-v3\",\n  \"selection_authority\": \"build-time-only; not a runtime capability grant\",\n  \"state\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(
      &builder, catalog != 0 ? "available" : "unavailable");
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ",\n  \"components\": [\n");
  for (index = 0U; status == CONFIT_OK && catalog != 0 && index < catalog->component_count;
       ++index) {
    const ConfitComponent *component = &catalog->components[index];
    status = confit_v2_builder_append(&builder, "    { \"id\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, component->id);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"kind\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(
        &builder, confit_component_kind_name(component->kind));
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"summary\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(
        &builder, component->summary);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"owner\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(
        &builder, component->owner);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"manifest\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, component->manifest_path);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"makefile\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, component->makefile_path);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"build_include\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, component->build_include);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"sources\": ");
    if (status == CONFIT_OK) status = confit_v4_append_component_atom_array(
        &builder, component->sources, component->source_count);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"public_headers\": ");
    if (status == CONFIT_OK) status = confit_v4_append_component_atom_array(
        &builder, component->public_headers, component->public_header_count);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"feature_requires\": ");
    if (status == CONFIT_OK) status = confit_v4_append_component_atom_array(
        &builder, component->feature_requires, component->feature_requirement_count);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"feature_provides\": ");
    if (status == CONFIT_OK) status = confit_v4_append_component_atom_array(
        &builder, component->feature_provides, component->feature_provide_count);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"feature_conflicts\": ");
    if (status == CONFIT_OK) status = confit_v4_append_component_atom_array(
        &builder, component->feature_conflicts, component->feature_conflict_count);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"kapi_requires\": ");
    if (status == CONFIT_OK) status = confit_v4_append_component_atom_array(
        &builder, component->kapi_requires, component->kapi_requirement_count);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"kapi_provides\": ");
    if (status == CONFIT_OK) status = confit_v4_append_component_atom_array(
        &builder, component->kapi_provides, component->kapi_provide_count);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, " } ");
    if (status == CONFIT_OK) status = confit_v2_builder_append(
        &builder, index + 1U == catalog->component_count ? "\n" : ",\n");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  ],\n  \"root_features\": ");
  if (status == CONFIT_OK) status = confit_v4_append_component_root_array(&builder, closure);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ",\n  \"selected\": ");
  if (status == CONFIT_OK) status = confit_v4_append_component_id_array(&builder, closure);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ",\n  \"reasons\": [\n");
  for (index = 0U; status == CONFIT_OK && closure != 0 && index < closure->reason_count;
       ++index) {
    const ConfitComponentReason *reason = &closure->reasons[index];
    status = confit_v2_builder_append(&builder, "    { \"component\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, reason->component_id);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"kind\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(
        &builder, confit_component_reason_kind_name(reason->kind));
    if (status == CONFIT_OK) status = confit_v2_builder_append(
        &builder, ", \"provider_selection\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(
        &builder, confit_component_provider_selection_name(
            reason->provider_selection));
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"from\": ");
    if (status == CONFIT_OK && reason->from_id != 0) {
      status = confit_v2_append_json_string(&builder, reason->from_id);
    } else if (status == CONFIT_OK) {
      status = confit_v2_builder_append(&builder, "null");
    }
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"requirement\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, reason->requirement);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"source\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, reason->source_path);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, ", \"line\": %llu, \"column\": %llu",
        (unsigned long long)reason->source_line,
        (unsigned long long)reason->source_column);
    if (status == CONFIT_OK) status = confit_v2_builder_append(
        &builder, index + 1U == closure->reason_count ? " }\n" : " },\n");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  ]");
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n}\n");
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_clear(&builder);
  return status;
}

static int confit_v4_is_safe_make_path(const char *text, int absolute) {
  size_t index;
  if (text == 0 || text[0] == '\0' || strlen(text) >= 4096U ||
      ((text[0] == '/') != absolute) ||
      (!absolute && (strcmp(text, "..") == 0 || strncmp(text, "../", 3U) == 0 ||
                     strcmp(text, ".") == 0 || strncmp(text, "./", 2U) == 0))) {
    return 0;
  }
  for (index = 0U; text[index] != '\0'; ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (!((value >= 'a' && value <= 'z') ||
          (value >= 'A' && value <= 'Z') ||
          (value >= '0' && value <= '9') || value == '/' || value == '.' ||
          value == '_' || value == '-' || value == '+' || value == '@')) {
      return 0;
    }
  }
  return strstr(text, "/../") == 0 && strstr(text, "/./") == 0 &&
         strcmp(text + (strlen(text) >= 3U ? strlen(text) - 3U : 0U), "/..") != 0 &&
         strcmp(text + (strlen(text) >= 2U ? strlen(text) - 2U : 0U), "/.") != 0;
}

static ConfitStatus confit_v4_generate_reason_json(
    const ConfitV2Snapshot *snapshot, const char *tool_identity,
    const ConfitComponentClosure *closure, char **out) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  size_t index;
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n");
  if (status == CONFIT_OK) status = confit_v4_append_identity(
      &builder, snapshot, "confit-reason-v4", tool_identity);
  if (status == CONFIT_OK) status = confit_v2_builder_append(
      &builder, ",\n  \"reasons\": [\n");
  for (index = 0U; status == CONFIT_OK && closure != 0 &&
                        index < closure->reason_count;
       ++index) {
    const ConfitComponentReason *reason = &closure->reasons[index];
    status = confit_v2_builder_append(&builder, "    { \"component\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(
        &builder, reason->component_id);
    if (status == CONFIT_OK) status = confit_v2_builder_append(
        &builder, ", \"kind\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(
        &builder, confit_component_reason_kind_name(reason->kind));
    if (status == CONFIT_OK) status = confit_v2_builder_append(
        &builder, ", \"provider_selection\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(
        &builder, confit_component_provider_selection_name(
            reason->provider_selection));
    if (status == CONFIT_OK) status = confit_v2_builder_append(
        &builder, ", \"from\": ");
    if (status == CONFIT_OK && reason->from_id != 0) {
      status = confit_v2_append_json_string(&builder, reason->from_id);
    } else if (status == CONFIT_OK) {
      status = confit_v2_builder_append(&builder, "null");
    }
    if (status == CONFIT_OK) status = confit_v2_builder_append(
        &builder, ", \"requirement\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(
        &builder, reason->requirement);
    if (status == CONFIT_OK) status = confit_v2_builder_append(
        &builder, ", \"source\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(
        &builder, reason->source_path);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, ", \"line\": %llu, \"column\": %llu }%s\n",
        (unsigned long long)reason->source_line,
        (unsigned long long)reason->source_column,
        index + 1U == closure->reason_count ? "" : ",");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(
      &builder, "  ]\n}\n");
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_v4_append_target_atom(
    ConfitV2ArtifactBuilder *builder, const char *name, const char *value,
    ConfitDiagnostic *diagnostic) {
  if (!confit_v4_is_safe_atom(value, 0)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, name, 0U, 0U,
                          "unsafe target atom cannot enter generated Make syntax");
    return CONFIT_ERR_SCHEMA;
  }
  return confit_v2_builder_appendf(builder, "%s:= %s\n", name, value);
}

static ConfitStatus confit_v4_append_target_path(
    ConfitV2ArtifactBuilder *builder, const char *name, const char *value,
    int absolute, ConfitDiagnostic *diagnostic) {
  if (value == 0) return confit_v2_builder_appendf(builder, "%s:=\n", name);
  if (!confit_v4_is_safe_make_path(value, absolute)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, name, 0U, 0U,
                          "unsafe target path cannot enter generated Make syntax");
    return CONFIT_ERR_SCHEMA;
  }
  return confit_v2_builder_appendf(builder, "%s:= %s\n", name, value);
}

static ConfitStatus confit_v4_generate_target_mk(
    const ConfitTargetPlan *plan, char **out, ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  size_t index;
  if (plan == 0) {
    *out = confit_v4_strdup(
        "# Generated by Confit; no target was selected.\n"
        "PARUS_TARGET_PLAN_ABI:= 0\n");
    return *out != 0 ? CONFIT_OK : CONFIT_ERR_INTERNAL;
  }
  if (plan->machine_executable != 0 &&
      (plan->machine_executable_path == 0 ||
       plan->machine_executable_sha256 == 0 ||
       !confit_v4_is_raw_sha256(plan->machine_executable_sha256) ||
       plan->machine_executable_version == 0)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                          plan->target_descriptor_path, 0U, 0U,
                          "machine executable identity is incomplete");
    return CONFIT_ERR_SCHEMA;
  }
  if (plan->compiler_sha256 == 0 || plan->compiler_version == 0 ||
      plan->archiver_sha256 == 0 || plan->archiver_version == 0 ||
      plan->linker_sha256 == 0 || plan->linker_version == 0 ||
      !confit_v4_is_raw_sha256(plan->compiler_sha256) ||
      !confit_v4_is_raw_sha256(plan->archiver_sha256) ||
      !confit_v4_is_raw_sha256(plan->linker_sha256) ||
      (plan->dtc_path != 0 &&
       (plan->dtc_sha256 == 0 || plan->dtc_version == 0 ||
        !confit_v4_is_raw_sha256(plan->dtc_sha256)))) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                          plan->toolchain_descriptor_path, 0U, 0U,
                          "toolchain executable identity is incomplete");
    return CONFIT_ERR_SCHEMA;
  }
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(
      &builder, "# Generated by Confit; closed target/toolchain tuple only.\n"
                "PARUS_TARGET_PLAN_ABI:= 3\n");
#define CONFIT_TARGET_ATOM(name, member)                                       \
  if (status == CONFIT_OK)                                                     \
    status = confit_v4_append_target_atom(&builder, name, plan->member,        \
                                          diagnostic)
  CONFIT_TARGET_ATOM("PARUS_TARGET_ID", target_id);
  CONFIT_TARGET_ATOM("PARUS_TARGET_ISA", isa);
  CONFIT_TARGET_ATOM("PARUS_TARGET_ABI", abi);
  CONFIT_TARGET_ATOM("PARUS_TARGET_CPU_PROFILE", cpu_profile);
  CONFIT_TARGET_ATOM("PARUS_TARGET_ENTRY_PROFILE", entry_profile);
  CONFIT_TARGET_ATOM("PARUS_TARGET_TOOLCHAIN_ID", toolchain_id);
  CONFIT_TARGET_ATOM("PARUS_TARGET_TOOLCHAIN_KIND", toolchain_kind);
  CONFIT_TARGET_ATOM("PARUS_TARGET_TRIPLE", target_triple);
  CONFIT_TARGET_ATOM("PARUS_TARGET_LINK_EMULATION", link_emulation);
  CONFIT_TARGET_ATOM("PARUS_TARGET_IMAGE_ARTIFACT_PROFILE", image_artifact_profile);
  CONFIT_TARGET_ATOM("PARUS_TARGET_PACKAGE_PROFILE", package_profile);
  CONFIT_TARGET_ATOM("PARUS_TARGET_MACHINE_PROFILE", machine_profile);
  CONFIT_TARGET_ATOM("PARUS_TARGET_EXPECTED_COMPONENT", expected_component);
  CONFIT_TARGET_ATOM("PARUS_TARGET_EXPECTED_CAPABILITY", expected_capability);
  CONFIT_TARGET_ATOM("PARUS_TARGET_SUPPORT_PROVIDER_OWNER", support_provider_owner);
  CONFIT_TARGET_ATOM("PARUS_TARGET_SUPPORT_CONSUMER_OWNER", support_consumer_owner);
  CONFIT_TARGET_ATOM("PARUS_TARGET_SUPPORT_ROLE", support_role);
  CONFIT_TARGET_ATOM("PARUS_TARGET_SUPPORT_REQUIRED_KAPI", support_required_kapi);
  CONFIT_TARGET_ATOM("PARUS_TARGET_OUTPUT_STEM", output_stem);
  CONFIT_TARGET_ATOM("PARUS_TARGET_REQUIRED_PROFILE", required_profile);
  CONFIT_TARGET_ATOM("PARUS_TARGET_KERNEL_ARTIFACT_PROFILE", kernel_artifact_profile);
  CONFIT_TARGET_ATOM("PARUS_TARGET_WORLD_ARTIFACT_PROFILE", world_artifact_profile);
#undef CONFIT_TARGET_ATOM
  if (status == CONFIT_OK) {
    status = confit_v2_builder_append(
        &builder, "PARUS_TARGET_IMAGE_ARTIFACT_ROLES:=");
  }
  for (index = 0U; status == CONFIT_OK &&
                        index < plan->image_artifact_role_count; ++index) {
    const char *separator = strchr(plan->image_artifact_roles[index], '=');
    const size_t role_size = separator != 0
        ? (size_t)(separator - plan->image_artifact_roles[index]) : 0U;
    if (separator == 0 || role_size == 0U || role_size >= 64U) {
      status = CONFIT_ERR_SCHEMA;
    } else {
      status = confit_v2_builder_appendf(&builder, " %.*s", (int)role_size,
                                         plan->image_artifact_roles[index]);
    }
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n");
  for (index = 0U; status == CONFIT_OK &&
                        index < plan->image_artifact_role_count; ++index) {
    const char *separator = strchr(plan->image_artifact_roles[index], '=');
    const size_t role_size = separator != 0
        ? (size_t)(separator - plan->image_artifact_roles[index]) : 0U;
    char variable[128];
    const int written = snprintf(variable, sizeof(variable),
                                 "PARUS_TARGET_IMAGE_ARTIFACT_ROLE_%.*s",
                                 (int)role_size,
                                 plan->image_artifact_roles[index]);
    if (separator == 0 || role_size == 0U || written < 0 ||
        (size_t)written >= sizeof(variable)) {
      status = CONFIT_ERR_SCHEMA;
    } else {
      status = confit_v4_append_target_path(
          &builder, variable, separator + 1, 0, diagnostic);
    }
  }
  if (status == CONFIT_OK) {
    status = confit_v2_builder_append(
        &builder, "PARUS_TARGET_PACKAGE_INPUT_DIGEST_ROLES:=");
  }
  for (index = 0U; status == CONFIT_OK &&
                        index < plan->package_input_digest_count; ++index) {
    const char *separator = strchr(plan->package_input_digests[index], '=');
    const size_t role_size = separator != 0
        ? (size_t)(separator - plan->package_input_digests[index]) : 0U;
    if (separator == 0 || role_size == 0U || role_size >= 64U) {
      status = CONFIT_ERR_SCHEMA;
    } else {
      status = confit_v2_builder_appendf(&builder, " %.*s", (int)role_size,
                                         plan->package_input_digests[index]);
    }
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n");
  for (index = 0U; status == CONFIT_OK &&
                        index < plan->package_input_digest_count; ++index) {
    const char *separator = strchr(plan->package_input_digests[index], '=');
    const size_t role_size = separator != 0
        ? (size_t)(separator - plan->package_input_digests[index]) : 0U;
    char variable[128];
    const int written = snprintf(variable, sizeof(variable),
                                 "PARUS_TARGET_PACKAGE_INPUT_SHA256_%.*s",
                                 (int)role_size,
                                 plan->package_input_digests[index]);
    if (separator == 0 || role_size == 0U || written < 0 ||
        (size_t)written >= sizeof(variable)) {
      status = CONFIT_ERR_SCHEMA;
    } else {
      status = confit_v4_append_target_atom(&builder, variable,
                                             separator + 1, diagnostic);
    }
  }
  if (status == CONFIT_OK) {
    status = confit_v2_builder_append(
        &builder, "PARUS_TARGET_KERNEL_ARTIFACT_ROLES:=");
  }
  for (index = 0U; status == CONFIT_OK &&
                        index < plan->kernel_artifact_role_count; ++index) {
    const char *separator = strchr(plan->kernel_artifact_roles[index], '=');
    const size_t role_size = separator != 0
        ? (size_t)(separator - plan->kernel_artifact_roles[index]) : 0U;
    char role[64];
    if (separator == 0 || role_size == 0U || role_size >= sizeof(role)) {
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    memcpy(role, plan->kernel_artifact_roles[index], role_size);
    role[role_size] = '\0';
    status = confit_v2_builder_appendf(&builder, " %s", role);
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n");
  for (index = 0U; status == CONFIT_OK &&
                        index < plan->kernel_artifact_role_count; ++index) {
    const char *separator = strchr(plan->kernel_artifact_roles[index], '=');
    const size_t role_size = separator != 0
        ? (size_t)(separator - plan->kernel_artifact_roles[index]) : 0U;
    char role[64];
    char variable[128];
    int written;
    if (separator == 0 || role_size == 0U || role_size >= sizeof(role)) {
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    memcpy(role, plan->kernel_artifact_roles[index], role_size);
    role[role_size] = '\0';
    written = snprintf(variable, sizeof(variable),
                       "PARUS_TARGET_KERNEL_ARTIFACT_ROLE_%s", role);
    if (written < 0 || (size_t)written >= sizeof(variable)) {
      status = CONFIT_ERR_INTERNAL;
      break;
    }
    status = confit_v4_append_target_path(
        &builder, variable, separator + 1, 0, diagnostic);
  }
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_WORLD_BOOT_COMPONENT",
      plan->world_boot_component != 0 ? plan->world_boot_component : "none",
      diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_WORLD_ARTIFACT_ENTRY",
      plan->world_artifact_entry != 0 ? plan->world_artifact_entry : "none",
      diagnostic);
  if (status == CONFIT_OK) {
    if (plan->world_artifact_linker_script != 0) {
      status = confit_v4_append_target_path(
          &builder, "PARUS_TARGET_WORLD_ARTIFACT_LINKER_SCRIPT",
          plan->world_artifact_linker_script, 0, diagnostic);
    } else {
      status = confit_v2_builder_append(
          &builder, "PARUS_TARGET_WORLD_ARTIFACT_LINKER_SCRIPT:= none\n");
    }
  }
  if (status == CONFIT_OK) {
    status = confit_v2_builder_append(&builder,
                                      "PARUS_TARGET_COMPILE_TUPLE:=");
  }
  for (index = 0U; status == CONFIT_OK && index < plan->compile_tuple_count;
       ++index) {
    status = confit_v2_builder_appendf(&builder, " %s",
                                       plan->compile_tuple[index]);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_builder_append(
        &builder, "\nPARUS_TARGET_WORLD_ARTIFACT_ROLES:=");
  }
  for (index = 0U; status == CONFIT_OK &&
                        index < plan->world_artifact_role_count; ++index) {
    const char *separator = strchr(plan->world_artifact_roles[index], '=');
    size_t role_size = separator != 0
        ? (size_t)(separator - plan->world_artifact_roles[index]) : 0U;
    char role[64];
    if (separator == 0 || role_size == 0U || role_size >= sizeof(role)) {
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    memcpy(role, plan->world_artifact_roles[index], role_size);
    role[role_size] = '\0';
    status = confit_v2_builder_appendf(&builder, " %s", role);
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n");
  for (index = 0U; status == CONFIT_OK &&
                        index < plan->world_artifact_role_count; ++index) {
    const char *separator = strchr(plan->world_artifact_roles[index], '=');
    size_t role_size = separator != 0
        ? (size_t)(separator - plan->world_artifact_roles[index]) : 0U;
    char role[64];
    if (separator == 0 || role_size == 0U || role_size >= sizeof(role)) {
      status = CONFIT_ERR_SCHEMA;
      break;
    }
    memcpy(role, plan->world_artifact_roles[index], role_size);
    role[role_size] = '\0';
    {
      char variable[128];
      const int written = snprintf(variable, sizeof(variable),
                                   "PARUS_TARGET_WORLD_ARTIFACT_ROLE_%s", role);
      if (written < 0 || (size_t)written >= sizeof(variable)) {
        status = CONFIT_ERR_INTERNAL;
      } else {
        status = confit_v4_append_target_path(
            &builder, variable, separator + 1, 0, diagnostic);
      }
    }
  }
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(
      &builder, "PARUS_TARGET_MAX_WORLD_BYTES:= %zu\n",
      plan->max_world_bytes);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_MACHINE_RUNNER",
      plan->machine_runner != 0 ? plan->machine_runner : "none", diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_MACHINE_ARCHITECTURE",
      plan->machine_architecture != 0 ? plan->machine_architecture : "none",
      diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_MACHINE_EXECUTABLE",
      plan->machine_executable != 0 ? plan->machine_executable : "none",
      diagnostic);
  if (status == CONFIT_OK && plan->machine_executable_path != 0) {
    status = confit_v4_append_target_path(
        &builder, "PARUS_TARGET_MACHINE_EXECUTABLE_PATH",
        plan->machine_executable_path, 1, diagnostic);
  } else if (status == CONFIT_OK) {
    status = confit_v4_append_target_atom(
        &builder, "PARUS_TARGET_MACHINE_EXECUTABLE_PATH", "none", diagnostic);
  }
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_MACHINE_EXECUTABLE_SHA256",
      plan->machine_executable_sha256 != 0
          ? plan->machine_executable_sha256
          : "none",
      diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_MACHINE_EXECUTABLE_VERSION",
      plan->machine_executable_version != 0
          ? plan->machine_executable_version
          : "none",
      diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_MACHINE_TRUST_PROFILE",
      plan->machine_trust_profile != 0 ? plan->machine_trust_profile : "none",
      diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_MACHINE_RESOURCE_IDENTITY",
      plan->machine_resource_identity != 0
          ? plan->machine_resource_identity
          : "none",
      diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_MACHINE_EVIDENCE_TRANSPORT",
      plan->machine_evidence_transport != 0
          ? plan->machine_evidence_transport
          : "none",
      diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_MACHINE_EVIDENCE_PROTOCOL",
      plan->machine_evidence_protocol != 0
          ? plan->machine_evidence_protocol
          : "none",
      diagnostic);
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(
      &builder, "PARUS_TARGET_MACHINE_EVIDENCE_MAX_BYTES:= %zu\n",
      plan->machine_evidence_max_bytes);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_MACHINE_NAME",
      plan->machine_name != 0 ? plan->machine_name : "none", diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_MACHINE_CPU",
      plan->machine_cpu != 0 ? plan->machine_cpu : "none", diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_MACHINE_SERIAL",
      plan->machine_serial != 0 ? plan->machine_serial : "none", diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_MACHINE_ARTIFACT",
      plan->machine_artifact != 0 ? plan->machine_artifact : "none",
      diagnostic);
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(
      &builder, "PARUS_TARGET_MACHINE_MEMORY_MIB:= %zu\n",
      plan->machine_memory_mib);
  if (status == CONFIT_OK) status = confit_v4_append_target_path(
      &builder, "PARUS_TARGET_CC", plan->compiler_path, 1, diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_CC_SHA256", plan->compiler_sha256, diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_CC_VERSION", plan->compiler_version, diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_path(
      &builder, "PARUS_TARGET_AR", plan->archiver_path, 1, diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_AR_SHA256", plan->archiver_sha256, diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_AR_VERSION", plan->archiver_version, diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_path(
      &builder, "PARUS_TARGET_LLD", plan->linker_path, 1, diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_LLD_SHA256", plan->linker_sha256, diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_LLD_VERSION", plan->linker_version, diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_path(
      &builder, "PARUS_TARGET_RESOURCE_INCLUDE", plan->resource_include_path, 1,
      diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_path(
      &builder, "PARUS_TARGET_SYSROOT_RELATIVE", plan->sysroot_path, 0,
      diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_path(
      &builder, "PARUS_TARGET_LINKER_SCRIPT", plan->linker_script, 0,
      diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_path(
      &builder, "PARUS_TARGET_DTS", plan->dts_path, 0, diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_path(
      &builder, "PARUS_TARGET_DTC", plan->dtc_path, 1, diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_DTC_SHA256",
      plan->dtc_sha256 != 0 ? plan->dtc_sha256 : "none", diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_atom(
      &builder, "PARUS_TARGET_DTC_VERSION",
      plan->dtc_version != 0 ? plan->dtc_version : "none", diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_path(
      &builder, "PARUS_TARGET_PACKAGE_SOURCE", plan->package_source, 0,
      diagnostic);
  if (status == CONFIT_OK) status = confit_v4_append_target_path(
      &builder, "PARUS_TARGET_SUPPORT_FACADE_INCLUDE_ROOT",
      plan->support_facade_include_root, 0, diagnostic);
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(
      &builder, "PARUS_TARGET_MAX_IMAGE_BYTES:= %llu\n"
                "PARUS_TARGET_MAX_KERNEL_BYTES:= %llu\n",
      (unsigned long long)plan->max_image_bytes,
      (unsigned long long)plan->max_kernel_bytes);
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_v4_generate_nucleus_mk(
    const ConfitNucleusCatalog *catalog,
    const ConfitComponentClosure *components, char **out,
    ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  size_t index;
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(
      &builder,
      "# Generated by Confit; mandatory KERN_UNIT source ownership only.\n"
      "PARUS_NUCLEUS_UNIT_IDS:=");
  for (index = 0U; status == CONFIT_OK && catalog != 0 &&
                  index < catalog->unit_count; ++index) {
    status = confit_v2_builder_appendf(&builder, " %s", catalog->units[index].id);
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n");
  for (index = 0U; status == CONFIT_OK && catalog != 0 &&
                  index < catalog->unit_count; ++index) {
    char identifier[160];
    size_t item;
    const ConfitNucleusUnit *unit = &catalog->units[index];
    status = confit_v4_component_make_identifier(
        unit->id, identifier, sizeof(identifier));
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder,
        "PARUS_NUCLEUS_%s_DIRECTORY:= %s\n"
        "PARUS_NUCLEUS_%s_MAKEFILE:= %s\n"
        "PARUS_NUCLEUS_%s_SRCS:=",
        identifier, unit->directory, identifier, unit->makefile_path,
        identifier);
    for (item = 0U; status == CONFIT_OK && item < unit->source_count; ++item)
      status = confit_v2_builder_appendf(&builder, " %s", unit->sources[item]);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, "\nPARUS_NUCLEUS_%s_USES:=", identifier);
    for (item = 0U; status == CONFIT_OK && item < unit->use_count; ++item)
      status = confit_v2_builder_appendf(&builder, " %s", unit->uses[item]);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, "\nPARUS_NUCLEUS_%s_LINK_USES:=", identifier);
    for (item = 0U; status == CONFIT_OK && item < unit->link_use_count; ++item) {
      const char *resolved = NULL;
      status = confit_v4_resolve_link_owner(
          components, catalog, unit->id, unit->link_uses[item], &resolved,
          diagnostic);
      if (status == CONFIT_OK && confit_v4_is_safe_atom(resolved, 0))
        status = confit_v2_builder_appendf(&builder, " %s", resolved);
      else if (status == CONFIT_OK)
        status = CONFIT_ERR_SCHEMA;
    }
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, "\nPARUS_NUCLEUS_%s_KAPI_IMPORTS:=", identifier);
    for (item = 0U; status == CONFIT_OK &&
                    item < unit->kapi_import_count; ++item)
      status = confit_v2_builder_appendf(&builder, " %s",
                                         unit->kapi_imports[item]);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, "\nPARUS_NUCLEUS_%s_KAPI_INCLUDE_ROOTS:=", identifier);
    for (item = 0U; status == CONFIT_OK &&
                    item < unit->kapi_import_count; ++item) {
      char provider_directory[1024];
      status = confit_v4_kapi_facade_directory(
          components, catalog, unit->kapi_imports[item], provider_directory,
          sizeof(provider_directory), diagnostic);
      if (status == CONFIT_OK)
        status = confit_v2_builder_appendf(&builder, " %s",
                                           provider_directory);
    }
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, "\nPARUS_NUCLEUS_%s_KAPI_EXPORTS:=", identifier);
    for (item = 0U; status == CONFIT_OK && item < unit->kapi_export_count; ++item)
      status = confit_v2_builder_appendf(&builder, " %s",
                                         unit->kapi_exports[item]);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n");
  }
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  if (status != CONFIT_OK && !confit_diagnostic_has_error(diagnostic))
    confit_diagnostic_set(diagnostic, status, 0, 0U, 0U,
                          "failed to generate nucleus Make adapter");
  confit_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_v4_generate_tests_mk(
    const ConfitTestCatalog *catalog, char **out,
    ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  size_t index;
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(
      &builder,
      "# Generated by Confit; tests are owner-local Make API v3 records.\n"
      "PARUS_TEST_IDS:=");
  for (index = 0U; status == CONFIT_OK && catalog != 0 &&
                  index < catalog->test_count; ++index)
    status = confit_v2_builder_appendf(&builder, " %s", catalog->tests[index].id);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n");
  for (index = 0U; status == CONFIT_OK && catalog != 0 &&
                  index < catalog->test_count; ++index) {
    const ConfitTestUnit *test = &catalog->tests[index];
    char identifier[160];
    size_t source;
    size_t private_use;
    status = confit_v4_component_make_identifier(test->id, identifier,
                                                  sizeof(identifier));
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder,
        "PARUS_TEST_%s_OWNER:= %s\n"
        "PARUS_TEST_%s_LANE:= %s\n"
        "PARUS_TEST_%s_EVIDENCE_CLASS:= %s\n"
        "PARUS_TEST_%s_TIMEOUT_MS:= %u\n"
        "PARUS_TEST_%s_SOURCE_DIR:= %s\n"
        "PARUS_TEST_%s_MAKEFILE:= %s\n"
        "PARUS_TEST_%s_BUILD_INCLUDE:= parus.test.mk\n"
        "PARUS_TEST_%s_TARGET:= %s\n"
        "PARUS_TEST_%s_MACHINE_PROFILE:= %s\n"
        "PARUS_TEST_%s_RECEIPT_PROFILE:= %s\n"
        "PARUS_TEST_%s_SRCS:=",
        identifier, test->owner, identifier, test->lane, identifier,
        test->evidence_class, identifier, test->timeout_ms, identifier,
        test->directory, identifier, test->makefile_path, identifier,
        identifier, test->target, identifier, test->machine_profile,
        identifier, test->receipt_profile, identifier);
    for (source = 0U; status == CONFIT_OK && source < test->source_count; ++source)
      status = confit_v2_builder_appendf(&builder, " %s", test->sources[source]);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n");
    if (status == CONFIT_OK)
      status = confit_v2_builder_appendf(
          &builder, "PARUS_TEST_%s_PRIVATE_USES:=", identifier);
    for (private_use = 0U;
         status == CONFIT_OK && private_use < test->private_use_count;
         ++private_use)
      status = confit_v2_builder_appendf(
          &builder, " %s", test->private_uses[private_use]);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n");
  }
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  if (status != CONFIT_OK && !confit_diagnostic_has_error(diagnostic))
    confit_diagnostic_set(diagnostic, status, 0, 0U, 0U,
                          "failed to generate local test Make adapter");
  confit_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_v4_generate_generators_mk(
    const ConfitGeneratorCatalog *catalog, char **out,
    ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  size_t index;
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(
      &builder,
      "# Generated by Confit; generators are reviewed typed actions.\n"
      "PARUS_GENERATOR_IDS:=");
  for (index = 0U; status == CONFIT_OK && catalog != 0 &&
                  index < catalog->generator_count; ++index)
    status = confit_v2_builder_appendf(&builder, " %s",
                                       catalog->generators[index].id);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n");
  for (index = 0U; status == CONFIT_OK && catalog != 0 &&
                  index < catalog->generator_count; ++index) {
    const ConfitGeneratorUnit *generator = &catalog->generators[index];
    char identifier[160];
    size_t item;
    status = confit_v4_component_make_identifier(generator->id, identifier,
                                                  sizeof(identifier));
    if (status == CONFIT_OK)
      status = confit_v2_builder_appendf(
          &builder,
          "PARUS_GENERATOR_%s_TOOL_ROLE:= %s\n"
          "PARUS_GENERATOR_%s_SOURCE_DIR:= %s\n"
          "PARUS_GENERATOR_%s_MAKEFILE:= %s\n"
          "PARUS_GENERATOR_%s_BUILD_INCLUDE:= parus.generator.mk\n"
          "PARUS_GENERATOR_%s_MAX_BYTES:= %u\n"
          "PARUS_GENERATOR_%s_INPUTS:=",
          identifier, generator->tool_role, identifier, generator->directory,
          identifier, generator->makefile_path, identifier, identifier,
          generator->max_bytes, identifier);
    for (item = 0U; status == CONFIT_OK && item < generator->input_count;
         ++item)
      status = confit_v2_builder_appendf(&builder, " %s",
                                         generator->inputs[item]);
    if (status == CONFIT_OK)
      status = confit_v2_builder_appendf(&builder,
                                         "\nPARUS_GENERATOR_%s_OUTPUTS:=",
                                         identifier);
    for (item = 0U; status == CONFIT_OK && item < generator->output_count;
         ++item)
      status = confit_v2_builder_appendf(&builder, " %s",
                                         generator->outputs[item]);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n");
    for (item = 0U; status == CONFIT_OK && item < generator->input_count;
         ++item) {
      char directory[4096];
      char physical[4096];
      char digest[65];
      status = confit_host_path_join(directory, sizeof(directory),
                                     catalog->project_root,
                                     generator->directory, diagnostic);
      if (status == CONFIT_OK)
        status = confit_host_path_join(physical, sizeof(physical), directory,
                                       generator->inputs[item], diagnostic);
      if (status == CONFIT_OK)
        status = confit_v4_sha256_file(physical, digest, diagnostic);
      if (status == CONFIT_OK)
        status = confit_v2_builder_appendf(
            &builder, "PARUS_GENERATOR_%s_INPUT_%llu_SHA256:= %s\n",
            identifier, (unsigned long long)item, digest);
    }
  }
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  if (status != CONFIT_OK && !confit_diagnostic_has_error(diagnostic))
    confit_diagnostic_set(diagnostic, status, 0, 0U, 0U,
                          "failed to generate typed generator Make adapter");
  confit_v2_builder_clear(&builder);
  return status;
}

typedef struct ConfitV4NamedText {
  const char *path;
  const char *text;
  char sha256[65];
} ConfitV4NamedText;

static void confit_v4_digest_named_texts(ConfitV4NamedText *texts,
                                         size_t text_count,
                                         char output[65]) {
  ConfitV4Sha256 hash;
  size_t index;

  confit_v4_sha256_init(&hash);
  confit_v4_sha256_update(&hash,
                          (const unsigned char *)"confit.bundle.v4.identity\n",
                          strlen("confit.bundle.v4.identity\n"));
  for (index = 0U; index < text_count; ++index) {
    const char separator = '\n';
    confit_v4_sha256_text(texts[index].text, texts[index].sha256);
    confit_v4_sha256_update(&hash, (const unsigned char *)texts[index].path,
                            strlen(texts[index].path));
    confit_v4_sha256_update(&hash, (const unsigned char *)&separator, 1U);
    confit_v4_sha256_update(&hash,
                            (const unsigned char *)texts[index].sha256,
                            strlen(texts[index].sha256));
    confit_v4_sha256_update(&hash, (const unsigned char *)&separator, 1U);
  }
  {
    static const char digits[] = "0123456789abcdef";
    unsigned char bytes[32];
    confit_v4_sha256_final(&hash, bytes);
    for (index = 0U; index < sizeof(bytes); ++index) {
      output[index * 2U] = digits[bytes[index] >> 4U];
      output[index * 2U + 1U] = digits[bytes[index] & 0x0FU];
    }
    output[64] = '\0';
  }
}

static ConfitStatus confit_v4_generate_config_mk(
    const ConfitV2Snapshot *snapshot, const char *bundle_digest, char **out,
    ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  const char *project = confit_v2_snapshot_project_name(snapshot);
  const char *profile = confit_v2_snapshot_profile_name(snapshot);
  const char *target = confit_v2_snapshot_target_name(snapshot);

  if (!confit_v4_is_safe_atom(project, 0) ||
      (profile != 0 && !confit_v4_is_safe_atom(profile, 0)) ||
      (target != 0 && !confit_v4_is_safe_atom(target, 0))) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, 0, 0U, 0U,
                          "unsafe identity cannot enter generated Make syntax");
    return CONFIT_ERR_SCHEMA;
  }
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder,
      "# Generated by Confit. DO NOT EDIT.\nCONFIT_ARTIFACT_ABI:= 4\nCONFIT_PROJECT:= ");
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, project);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\nCONFIT_PROFILE:= ");
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, profile != 0 ? profile : "none");
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\nCONFIT_TARGET:= ");
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, target != 0 ? target : "none");
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(
      &builder, "\nCONFIT_BUNDLE_SHA256:= %s\n.include \"${.PARSEDIR}/config.values.mk\"\n.include \"${.PARSEDIR}/nucleus.mk\"\n.include \"${.PARSEDIR}/components.mk\"\n.include \"${.PARSEDIR}/target.mk\"\n.include \"${.PARSEDIR}/build.policy.mk\"\n.include \"${.PARSEDIR}/tests.mk\"\n.include \"${.PARSEDIR}/generators.mk\"\n",
      bundle_digest);
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_v4_generate_bundle_manifest(
    const ConfitV2Snapshot *snapshot, const char *tool_identity,
    const ConfitV4NamedText *texts, size_t text_count, const char *bundle_digest,
    char **out) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  size_t index;

  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n");
  if (status == CONFIT_OK) status = confit_v4_append_identity(
      &builder, snapshot, "confit-bundle-v4", tool_identity);
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(
      &builder, ",\n  \"bundle_digest\": \"sha256:%s\",\n  \"identity_preimage\": \"semantic-artifact-digests-v1\",\n  \"artifacts\": [\n",
      bundle_digest);
  for (index = 0U; status == CONFIT_OK && index < text_count; ++index) {
    status = confit_v2_builder_append(&builder, "    { \"path\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, texts[index].path);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, ", \"sha256\": \"sha256:%s\", \"size\": %llu }%s\n",
        texts[index].sha256, (unsigned long long)strlen(texts[index].text),
        index + 1U == text_count ? "" : ",");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  ]\n}\n");
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_clear(&builder);
  return status;
}

ConfitStatus confit_v4_generate_artifacts(
    const ConfitV2Snapshot *snapshot, const ConfitV4ArtifactOptions *options,
    ConfitV4ArtifactSet *out_artifacts, ConfitDiagnostic *diagnostic) {
  ConfitV4ArtifactOptions defaults;
  ConfitV4NamedText identity_texts[14];
  char component_catalog_digest[65] = {0};
  char configuration_digest[65] = {0};
  char target_digest[65] = {0};
  const char *tool_identity;
  ConfitStatus status = CONFIT_OK;

  if (snapshot == 0 || out_artifacts == 0 ||
      confit_v2_snapshot_project_name(snapshot) == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          "invalid sealed artifact v4 argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(out_artifacts, 0, sizeof(*out_artifacts));
  memset(&defaults, 0, sizeof(defaults));
  options = options != 0 ? options : &defaults;
  tool_identity = options->tool_identity != 0 ? options->tool_identity
                                              : CONFIT_VERSION_RELEASE;
  if (!confit_v4_is_safe_atom(tool_identity, 0)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, tool_identity,
                          0U, 0U, "unsafe tool identity");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  status = confit_v4_generate_header(snapshot, &out_artifacts->config_header,
                                     diagnostic);
  if (status == CONFIT_OK &&
      confit_v2_snapshot_target_name(snapshot) != 0) {
    status = confit_target_plan_validate_selection(
        options->target_plan, options->component_catalog,
        options->component_closure, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v4_generate_component_catalog_json(
        options->component_catalog, options->component_closure,
        &out_artifacts->component_catalog_json);
    if (status == CONFIT_OK) {
      confit_v4_sha256_text(out_artifacts->component_catalog_json,
                            component_catalog_digest);
    }
  }
  if (status == CONFIT_OK) {
    status = confit_v4_generate_selection(snapshot, tool_identity,
                                          options->component_catalog,
                                          options->component_closure,
                                          options, component_catalog_digest,
                                          &out_artifacts->selection_json);
  }
  if (status == CONFIT_OK) {
    status = confit_v4_generate_reason_json(
        snapshot, tool_identity, options->component_closure,
        &out_artifacts->reason_json);
  }
  if (status == CONFIT_OK) {
    status = confit_v4_generate_report(snapshot, tool_identity,
                                       options->component_catalog,
                                       options->component_closure,
                                       &out_artifacts->report_json);
    if (status == CONFIT_OK) status = confit_v4_generate_inputs(
        snapshot, options, tool_identity, &out_artifacts->inputs_json, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v4_generate_make_values(snapshot,
                                            &out_artifacts->config_values_mk,
                                            diagnostic);
    if (status == CONFIT_OK) status = confit_v4_generate_components_mk(
        options->component_catalog, options->component_closure,
        options->nucleus_catalog,
        &out_artifacts->components_mk, diagnostic);
    if (status == CONFIT_OK) status = confit_v4_generate_nucleus_mk(
        options->nucleus_catalog, options->component_closure,
        &out_artifacts->nucleus_mk, diagnostic);
    if (status == CONFIT_OK) status = confit_v4_generate_target_mk(
        options->target_plan, &out_artifacts->target_mk, diagnostic);
    if (status == CONFIT_OK) {
      confit_v4_sha256_text(out_artifacts->selection_json,
                            configuration_digest);
      confit_v4_sha256_text(out_artifacts->target_mk, target_digest);
      if (options->target_plan != 0) {
        const char *profile_name = confit_v2_snapshot_profile_name(snapshot);
        status = confit_build_policy_generate(
            profile_name != 0 ? profile_name : "none", configuration_digest,
            options->target_plan, target_digest,
            &out_artifacts->build_policy,
            out_artifacts->build_policy_digest, diagnostic);
      } else {
        out_artifacts->build_policy = confit_v4_strdup(
            "schema=parus-build-policy-v1\npolicy_abi=0\nstate=unavailable\n");
        if (out_artifacts->build_policy == 0) status = CONFIT_ERR_INTERNAL;
        if (status == CONFIT_OK) {
          confit_v4_sha256_text(out_artifacts->build_policy,
                                out_artifacts->build_policy_digest);
        }
      }
    }
    if (status == CONFIT_OK) {
      status = confit_build_policy_generate_make_adapter(
          out_artifacts->build_policy_digest, configuration_digest,
          target_digest, &out_artifacts->build_policy_mk, diagnostic);
    }
    if (status == CONFIT_OK) status = confit_v4_generate_tests_mk(
        options->test_catalog, &out_artifacts->tests_mk, diagnostic);
    if (status == CONFIT_OK) status = confit_v4_generate_generators_mk(
        options->generator_catalog, &out_artifacts->generators_mk, diagnostic);
  }
  if (status == CONFIT_OK) {
    identity_texts[0] = (ConfitV4NamedText){"config.h", out_artifacts->config_header, {0}};
    identity_texts[1] = (ConfitV4NamedText){"config.selection.json", out_artifacts->selection_json, {0}};
    identity_texts[2] = (ConfitV4NamedText){"config.reason.json", out_artifacts->reason_json, {0}};
    identity_texts[3] = (ConfitV4NamedText){"config.report.json", out_artifacts->report_json, {0}};
    identity_texts[4] = (ConfitV4NamedText){"config.inputs.json", out_artifacts->inputs_json, {0}};
    identity_texts[5] = (ConfitV4NamedText){"config.values.mk", out_artifacts->config_values_mk, {0}};
    identity_texts[6] = (ConfitV4NamedText){"nucleus.mk", out_artifacts->nucleus_mk, {0}};
    identity_texts[7] = (ConfitV4NamedText){"components.mk", out_artifacts->components_mk, {0}};
    identity_texts[8] = (ConfitV4NamedText){"target.mk", out_artifacts->target_mk, {0}};
    identity_texts[9] = (ConfitV4NamedText){"build.policy", out_artifacts->build_policy, {0}};
    identity_texts[10] = (ConfitV4NamedText){"build.policy.mk", out_artifacts->build_policy_mk, {0}};
    identity_texts[11] = (ConfitV4NamedText){"tests.mk", out_artifacts->tests_mk, {0}};
    identity_texts[12] = (ConfitV4NamedText){"generators.mk", out_artifacts->generators_mk, {0}};
    identity_texts[13] = (ConfitV4NamedText){"component.catalog.json", out_artifacts->component_catalog_json, {0}};
    confit_v4_digest_named_texts(identity_texts,
                                 sizeof(identity_texts) / sizeof(identity_texts[0]),
                                 out_artifacts->bundle_digest);
    status = confit_v4_generate_config_mk(snapshot, out_artifacts->bundle_digest,
                                          &out_artifacts->config_mk, diagnostic);
    if (status == CONFIT_OK) {
      ConfitV4NamedText manifest_texts[15];
      memcpy(manifest_texts, identity_texts, sizeof(identity_texts));
      manifest_texts[14] = (ConfitV4NamedText){"config.mk", out_artifacts->config_mk, {0}};
      for (size_t index = 0U; index < sizeof(manifest_texts) / sizeof(manifest_texts[0]); ++index) {
        confit_v4_sha256_text(manifest_texts[index].text, manifest_texts[index].sha256);
      }
      status = confit_v4_generate_bundle_manifest(
          snapshot, tool_identity, manifest_texts,
          sizeof(manifest_texts) / sizeof(manifest_texts[0]),
          out_artifacts->bundle_digest, &out_artifacts->bundle_json);
    }
  }
  if (status != CONFIT_OK) {
    confit_v4_artifact_set_clear(out_artifacts);
    if (diagnostic != 0 && diagnostic->message == 0) {
      confit_diagnostic_set(diagnostic, status, 0, 0U, 0U,
                            "failed to generate sealed artifact v4 bundle");
    }
  }
  return status;
}

void confit_v4_artifact_set_clear(ConfitV4ArtifactSet *artifacts) {
  if (artifacts == 0) return;
  free(artifacts->config_header);
  free(artifacts->selection_json);
  free(artifacts->reason_json);
  free(artifacts->report_json);
  free(artifacts->inputs_json);
  free(artifacts->config_mk);
  free(artifacts->config_values_mk);
  free(artifacts->components_mk);
  free(artifacts->nucleus_mk);
  free(artifacts->target_mk);
  free(artifacts->build_policy);
  free(artifacts->build_policy_mk);
  free(artifacts->tests_mk);
  free(artifacts->generators_mk);
  free(artifacts->component_catalog_json);
  free(artifacts->bundle_json);
  memset(artifacts, 0, sizeof(*artifacts));
}

static int confit_v4_directory_exists(const char *path) {
  struct stat state;
  return stat(path, &state) == 0 && S_ISDIR(state.st_mode);
}

static int confit_v4_make_directory_once(const char *path) {
#if defined(_WIN32)
  return _mkdir(path) == 0;
#else
  return mkdir(path, 0755) == 0;
#endif
}

static ConfitStatus confit_v4_make_unique_staging(
    const char *staging_parent, const char *bundle_digest, char *out,
    size_t out_size, ConfitDiagnostic *diagnostic) {
  size_t attempt;

  for (attempt = 0U; attempt < 1024U; ++attempt) {
    const int written = snprintf(out, out_size, "%s/%s.%04llu.staging",
                                 staging_parent, bundle_digest,
                                 (unsigned long long)attempt);
    if (written < 0 || (size_t)written >= out_size) break;
    if (confit_v4_make_directory_once(out)) return CONFIT_OK;
    if (errno != EEXIST) break;
  }
  confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, staging_parent,
                        0U, 0U, "failed to allocate private sealed-bundle staging directory");
  return CONFIT_ERR_GENERATION;
}

static ConfitStatus confit_v4_join_path(char *out, size_t out_size,
                                        const char *left, const char *right,
                                        ConfitDiagnostic *diagnostic) {
  return confit_host_path_join(out, out_size, left, right, diagnostic);
}

static ConfitStatus confit_v4_verify_text(const char *path, const char *text,
                                          ConfitDiagnostic *diagnostic) {
  char *actual = 0;
  ConfitStatus status = confit_host_read_text_file(path, &actual, 0, diagnostic);
  if (status == CONFIT_OK && strcmp(actual, text) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, path, 0U, 0U,
                          "published sealed artifact bytes differ from its identity");
    status = CONFIT_ERR_GENERATION;
  }
  confit_host_free(actual);
  return status;
}

static ConfitStatus confit_v4_complete_texts(
    const ConfitV4ArtifactSet *artifacts,
    ConfitV4NamedText texts[CONFIT_V4_PUBLISHED_TEXT_COUNT],
    ConfitDiagnostic *diagnostic) {
  if (artifacts == 0 || artifacts->bundle_digest[0] == '\0' ||
      strlen(artifacts->bundle_digest) != 64U || artifacts->config_header == 0 ||
      artifacts->selection_json == 0 || artifacts->reason_json == 0 ||
      artifacts->report_json == 0 ||
      artifacts->inputs_json == 0 || artifacts->config_mk == 0 ||
      artifacts->config_values_mk == 0 || artifacts->nucleus_mk == 0 ||
      artifacts->components_mk == 0 ||
      artifacts->target_mk == 0 || artifacts->build_policy == 0 ||
      artifacts->build_policy_mk == 0 || artifacts->tests_mk == 0 ||
      artifacts->generators_mk == 0 ||
      artifacts->component_catalog_json == 0 || artifacts->bundle_json == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          "sealed publication requires the complete ABI v4 artifact set");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  texts[0] = (ConfitV4NamedText){"config.h", artifacts->config_header, {0}};
  texts[1] = (ConfitV4NamedText){"config.selection.json", artifacts->selection_json, {0}};
  texts[2] = (ConfitV4NamedText){"config.reason.json", artifacts->reason_json, {0}};
  texts[3] = (ConfitV4NamedText){"config.report.json", artifacts->report_json, {0}};
  texts[4] = (ConfitV4NamedText){"config.inputs.json", artifacts->inputs_json, {0}};
  texts[5] = (ConfitV4NamedText){"config.mk", artifacts->config_mk, {0}};
  texts[6] = (ConfitV4NamedText){"config.values.mk", artifacts->config_values_mk, {0}};
  texts[7] = (ConfitV4NamedText){"nucleus.mk", artifacts->nucleus_mk, {0}};
  texts[8] = (ConfitV4NamedText){"components.mk", artifacts->components_mk, {0}};
  texts[9] = (ConfitV4NamedText){"target.mk", artifacts->target_mk, {0}};
  texts[10] = (ConfitV4NamedText){"build.policy", artifacts->build_policy, {0}};
  texts[11] = (ConfitV4NamedText){"build.policy.mk", artifacts->build_policy_mk, {0}};
  texts[12] = (ConfitV4NamedText){"tests.mk", artifacts->tests_mk, {0}};
  texts[13] = (ConfitV4NamedText){"generators.mk", artifacts->generators_mk, {0}};
  texts[14] = (ConfitV4NamedText){"component.catalog.json", artifacts->component_catalog_json, {0}};
  texts[15] = (ConfitV4NamedText){"config.bundle.json", artifacts->bundle_json, {0}};
  return CONFIT_OK;
}

static ConfitStatus confit_v4_verify_generation(
    const char *generation,
    const ConfitV4NamedText texts[CONFIT_V4_PUBLISHED_TEXT_COUNT],
    ConfitDiagnostic *diagnostic) {
  size_t index;
  ConfitStatus status = CONFIT_OK;
  char path[4096];

  for (index = 0U; status == CONFIT_OK &&
                  index < CONFIT_V4_PUBLISHED_TEXT_COUNT; ++index) {
    status = confit_v4_join_path(path, sizeof(path), generation, texts[index].path,
                                 diagnostic);
    if (status == CONFIT_OK) status = confit_v4_verify_text(path, texts[index].text,
                                                            diagnostic);
  }
  return status;
}

static ConfitStatus confit_v4_write_staging(
    const char *staging,
    const ConfitV4NamedText texts[CONFIT_V4_PUBLISHED_TEXT_COUNT],
    size_t fault_after_artifact, ConfitDiagnostic *diagnostic) {
  size_t index;
  ConfitStatus status = CONFIT_OK;
  char path[4096];

  for (index = 0U; status == CONFIT_OK &&
                  index < CONFIT_V4_PUBLISHED_TEXT_COUNT; ++index) {
    status = confit_v4_join_path(path, sizeof(path), staging, texts[index].path,
                                 diagnostic);
    if (status == CONFIT_OK) status = confit_host_write_text_file(path, texts[index].text,
                                                                   diagnostic);
    if (status == CONFIT_OK && fault_after_artifact != 0U &&
        index + 1U == fault_after_artifact) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, staging, 0U, 0U,
                            "test fault injected before sealed bundle publication");
      status = CONFIT_ERR_GENERATION;
    }
  }
  if (status == CONFIT_OK) status = confit_v4_verify_generation(staging, texts, diagnostic);
  return status;
}

static void confit_v4_mark_read_only(const char *generation,
    const ConfitV4NamedText texts[CONFIT_V4_PUBLISHED_TEXT_COUNT]) {
#if !defined(_WIN32)
  char path[4096];
  size_t index;
  for (index = 0U; index < CONFIT_V4_PUBLISHED_TEXT_COUNT; ++index) {
    if (snprintf(path, sizeof(path), "%s/%s", generation, texts[index].path) > 0) {
      (void)chmod(path, 0444);
    }
  }
  (void)chmod(generation, 0555);
#else
  (void)generation;
  (void)texts;
#endif
}

static int confit_v4_remove_staging(const char *staging,
    const ConfitV4NamedText texts[CONFIT_V4_PUBLISHED_TEXT_COUNT]) {
  char path[4096];
  size_t index;

  for (index = 0U; index < CONFIT_V4_PUBLISHED_TEXT_COUNT; ++index) {
    if (snprintf(path, sizeof(path), "%s/%s", staging, texts[index].path) <= 0 ||
        remove(path) != 0) {
      if (errno != ENOENT) return 0;
    }
  }
#if defined(_WIN32)
  return _rmdir(staging) == 0 || errno == ENOENT;
#else
  return rmdir(staging) == 0 || errno == ENOENT;
#endif
}

static ConfitStatus confit_v4_publish_selected_alias(
    const char *selected, const char *relative_generation, int *out_changed,
    ConfitDiagnostic *diagnostic) {
  char temporary[4096];

  *out_changed = 0;
  if (snprintf(temporary, sizeof(temporary), "%s.confit-tmp", selected) <= 0 ||
      strlen(temporary) >= sizeof(temporary) - 1U) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, selected, 0U, 0U,
                          "selected alias path is too long");
    return CONFIT_ERR_GENERATION;
  }
#if defined(_WIN32)
  if (GetFileAttributesA(temporary) != INVALID_FILE_ATTRIBUTES) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, temporary, 0U, 0U,
                          "selected alias staging path already exists");
    return CONFIT_ERR_GENERATION;
  }
  if (CreateSymbolicLinkA(temporary, relative_generation,
                          SYMBOLIC_LINK_FLAG_DIRECTORY |
                              SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, temporary, 0U, 0U,
                          "failed to create selected directory alias");
    return CONFIT_ERR_GENERATION;
  }
  if (MoveFileExA(temporary, selected,
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
    (void)DeleteFileA(temporary);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, selected, 0U, 0U,
                          "failed to atomically publish selected directory alias");
    return CONFIT_ERR_GENERATION;
  }
#else
  {
    struct stat state;
    char current[128];
    const ssize_t current_size = readlink(selected, current, sizeof(current) - 1U);

    if (current_size >= 0) {
      current[(size_t)current_size] = '\0';
      if (strcmp(current, relative_generation) == 0) return CONFIT_OK;
    } else if (lstat(selected, &state) == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, selected, 0U, 0U,
                            "selected path is not a directory alias");
      return CONFIT_ERR_GENERATION;
    }
    if (lstat(temporary, &state) == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, temporary, 0U, 0U,
                            "selected alias staging path already exists");
      return CONFIT_ERR_GENERATION;
    }
    if (symlink(relative_generation, temporary) != 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, temporary, 0U, 0U,
                            "failed to create selected directory alias");
      return CONFIT_ERR_GENERATION;
    }
    if (rename(temporary, selected) != 0) {
      (void)unlink(temporary);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, selected, 0U, 0U,
                            "failed to atomically publish selected directory alias");
      return CONFIT_ERR_GENERATION;
    }
  }
#endif
  *out_changed = 1;
  return CONFIT_OK;
}

#if !defined(_WIN32)
static int confit_v4_posix_open_dir(int parent, const char *name, int create) {
  int descriptor = openat(parent, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                           O_NOFOLLOW);
  if (descriptor < 0 && create && errno == ENOENT) {
    if (mkdirat(parent, name, 0700) != 0 && errno != EEXIST) return -1;
    descriptor = openat(parent, name, O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                         O_NOFOLLOW);
  }
  return descriptor;
}

static int confit_v4_posix_open_absolute_root(const char *path, int create) {
  char copy[4096];
  char *cursor;
  int current;
  const size_t size = path != 0 ? strlen(path) : 0U;
  if (size < 2U || size >= sizeof(copy) || path[0] != '/' ||
      path[size - 1U] == '/' || strstr(path, "//") != 0) {
    errno = EINVAL;
    return -1;
  }
  memcpy(copy, path + 1, size);
  current = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (current < 0) return -1;
  cursor = copy;
  while (cursor != 0) {
    char *separator = strchr(cursor, '/');
    int next;
    if (separator != 0) *separator = '\0';
    if (cursor[0] == '\0' || strcmp(cursor, ".") == 0 ||
        strcmp(cursor, "..") == 0) {
      (void)close(current);
      errno = EINVAL;
      return -1;
    }
    next = confit_v4_posix_open_dir(current, cursor, create);
    (void)close(current);
    if (next < 0) return -1;
    current = next;
    cursor = separator != 0 ? separator + 1 : 0;
  }
  return current;
}

static int confit_v4_posix_sync_dir(int descriptor) {
  if (fsync(descriptor) == 0) return 1;
  return errno == EINVAL || errno == ENOTSUP;
}

static int confit_v4_posix_write_text(int directory, const char *name,
                                      const char *text) {
  const size_t size = strlen(text);
  size_t offset = 0U;
  int descriptor = openat(directory, name,
                          O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                          0600);
  if (descriptor < 0) return 0;
  while (offset < size) {
    const ssize_t amount = write(descriptor, text + offset, size - offset);
    if (amount <= 0) {
      (void)close(descriptor);
      return 0;
    }
    offset += (size_t)amount;
  }
  if (fsync(descriptor) != 0 || fchmod(descriptor, 0444) != 0 ||
      close(descriptor) != 0) return 0;
  return 1;
}

static int confit_v4_posix_verify_text(int directory, const char *name,
                                       const char *text) {
  struct stat metadata;
  const size_t expected = strlen(text);
  size_t offset = 0U;
  char buffer[4096];
  int descriptor = openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (descriptor < 0 || fstat(descriptor, &metadata) != 0 ||
      !S_ISREG(metadata.st_mode) || metadata.st_nlink != 1U ||
      metadata.st_size < 0 || (uint64_t)metadata.st_size != (uint64_t)expected) {
    if (descriptor >= 0) (void)close(descriptor);
    return 0;
  }
  while (offset < expected) {
    size_t want = expected - offset;
    ssize_t amount;
    if (want > sizeof(buffer)) want = sizeof(buffer);
    amount = read(descriptor, buffer, want);
    if (amount <= 0 || memcmp(buffer, text + offset, (size_t)amount) != 0) {
      (void)close(descriptor);
      return 0;
    }
    offset += (size_t)amount;
  }
  return close(descriptor) == 0;
}

static int confit_v4_posix_remove_generation(int generations,
                                             const char *name) {
  size_t count = 0U;
  int generation = confit_v4_posix_open_dir(generations, name, 0);
  if (generation < 0 || fchmod(generation, 0700) != 0) {
    if (generation >= 0) (void)close(generation);
    return 0;
  }
  {
    DIR *stream = fdopendir(dup(generation));
    struct dirent *entry;
    if (stream == 0) { (void)close(generation); return 0; }
    while ((entry = readdir(stream)) != 0) {
      struct stat metadata;
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
      if (++count > 64U ||
          fstatat(generation, entry->d_name, &metadata,
                  AT_SYMLINK_NOFOLLOW) != 0 ||
          !S_ISREG(metadata.st_mode) || metadata.st_nlink != 1U ||
          unlinkat(generation, entry->d_name, 0) != 0) {
        (void)closedir(stream); (void)close(generation); return 0;
      }
    }
    (void)closedir(stream);
  }
  (void)close(generation);
  return unlinkat(generations, name, AT_REMOVEDIR) == 0;
}

static int confit_v4_posix_gc_generations(int generations,
                                          const char *selected_digest) {
  DIR *stream = fdopendir(dup(generations));
  struct dirent *entry;
  size_t count = 0U;
  if (stream == 0) return 0;
  while ((entry = readdir(stream)) != 0) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
    if (++count > 64U || !confit_v4_is_raw_sha256(entry->d_name)) {
      (void)closedir(stream);
      return 0;
    }
    if (strcmp(entry->d_name, selected_digest) != 0 &&
        !confit_v4_posix_remove_generation(generations, entry->d_name)) {
      (void)closedir(stream);
      return 0;
    }
  }
  return closedir(stream) == 0;
}

static ConfitStatus confit_v4_publish_artifacts_posix(
    const ConfitV4PublishOptions *options, const ConfitV4ArtifactSet *artifacts,
    const ConfitV4NamedText texts[CONFIT_V4_PUBLISHED_TEXT_COUNT],
    size_t *out_changed_file_count,
    ConfitDiagnostic *diagnostic) {
  static unsigned long serial;
  char staging_name[128];
  char selected_target[96];
  char selected_temp[128];
  struct flock lock;
  int root = -1;
  int generations = -1;
  int staging_parent = -1;
  int staging = -1;
  int generation = -1;
  int lock_fd = -1;
  int created = 0;
  int selected_changed = 0;
  size_t index;
  ConfitStatus status = CONFIT_OK;
  const char *phase = "output-root";
  uint64_t generation_bytes = 0U;
  for (index = 0U; index < CONFIT_V4_PUBLISHED_TEXT_COUNT; ++index) {
    const size_t size = strlen(texts[index].text);
    if ((uint64_t)size > UINT64_MAX - generation_bytes) {
      return CONFIT_ERR_GENERATION;
    }
    generation_bytes += (uint64_t)size;
  }
  /* Old selected generation과 새 staging이 동시에 존재하는 atomic switch 중에도
   * config subtree가 4 MiB를 넘지 않도록 한 generation을 2 MiB로 닫는다. */
  if (generation_bytes > UINT64_C(2) * 1024U * 1024U) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION,
                          options->output_root, 0U, 0U,
                          "sealed generation exceeds the 2 MiB action quota");
    return CONFIT_ERR_GENERATION;
  }
  root = confit_v4_posix_open_absolute_root(options->output_root, 1);
  if (root < 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION,
                          options->output_root, 0U, 0U,
                          "output root is not one canonical no-follow directory");
    return CONFIT_ERR_GENERATION;
  }
  phase = "publisher-lock";
  if (status == CONFIT_OK) {
    struct stat lock_metadata;
    lock_fd = openat(root, ".confit-publish-v1.lock",
                     O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock_fd < 0 || fstat(lock_fd, &lock_metadata) != 0 ||
        !S_ISREG(lock_metadata.st_mode) || lock_metadata.st_nlink != 1U) {
      status = CONFIT_ERR_GENERATION;
    }
  }
  if (status == CONFIT_OK) {
    (void)memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    while (fcntl(lock_fd, F_SETLKW, &lock) != 0) {
      if (errno != EINTR) { status = CONFIT_ERR_GENERATION; break; }
    }
  }
  if (status == CONFIT_OK) generations = confit_v4_posix_open_dir(root, "generations", 1);
  if (status == CONFIT_OK) staging_parent = confit_v4_posix_open_dir(root, ".staging", 1);
  if (generations < 0 || staging_parent < 0) status = CONFIT_ERR_GENERATION;
  phase = "generation-open-or-stage";
  if (status == CONFIT_OK) {
    generation = confit_v4_posix_open_dir(generations, artifacts->bundle_digest, 0);
    if (generation >= 0) {
      for (index = 0U; status == CONFIT_OK &&
                      index < CONFIT_V4_PUBLISHED_TEXT_COUNT; ++index) {
        if (!confit_v4_posix_verify_text(generation, texts[index].path,
                                         texts[index].text)) status = CONFIT_ERR_GENERATION;
      }
      phase = "generation-rename";
    } else if (errno != ENOENT) {
      status = CONFIT_ERR_GENERATION;
    } else {
      const int length = snprintf(staging_name, sizeof(staging_name),
                                  "%s.%ld.%lu", artifacts->bundle_digest,
                                  (long)getpid(), ++serial);
      if (length <= 0 || (size_t)length >= sizeof(staging_name) ||
          mkdirat(staging_parent, staging_name, 0700) != 0) {
        status = CONFIT_ERR_GENERATION;
      } else {
        staging = confit_v4_posix_open_dir(staging_parent, staging_name, 0);
        if (staging < 0) status = CONFIT_ERR_GENERATION;
      }
      phase = "generation-write";
      for (index = 0U; status == CONFIT_OK &&
                      index < CONFIT_V4_PUBLISHED_TEXT_COUNT; ++index) {
        if (!confit_v4_posix_write_text(staging, texts[index].path,
                                        texts[index].text)) {
          status = CONFIT_ERR_GENERATION;
        } else if (options->fault_after_artifact != 0U &&
                   index + 1U == options->fault_after_artifact) {
          status = CONFIT_ERR_GENERATION;
        }
      }
      if (status == CONFIT_OK) {
        phase = "generation-seal-sync";
        if (!confit_v4_posix_sync_dir(staging)) status = CONFIT_ERR_GENERATION;
      }
      if (status == CONFIT_OK) {
        phase = "generation-rename";
        if (renameat(staging_parent, staging_name, generations,
                     artifacts->bundle_digest) != 0) {
          if (errno == EEXIST) phase = "generation-rename-exists";
          else if (errno == EACCES || errno == EPERM) phase = "generation-rename-permission";
          else if (errno == ENOENT) phase = "generation-rename-missing";
          status = CONFIT_ERR_GENERATION;
        }
      }
      if (status == CONFIT_OK) {
        created = 1;
        (void)close(staging); staging = -1;
        generation = confit_v4_posix_open_dir(generations,
                                               artifacts->bundle_digest, 0);
        phase = "generation-seal-mode";
        if (generation < 0 || fchmod(generation, 0555) != 0) {
          status = CONFIT_ERR_GENERATION;
        }
      }
      if (status != CONFIT_OK && staging >= 0) {
        (void)fchmod(staging, 0700);
        for (index = 0U; index < CONFIT_V4_PUBLISHED_TEXT_COUNT; ++index) {
          (void)unlinkat(staging, texts[index].path, 0);
        }
        (void)close(staging); staging = -1;
        (void)unlinkat(staging_parent, staging_name, AT_REMOVEDIR);
      }
    }
  }
  if (status == CONFIT_OK) {
    phase = "selected-alias";
    ssize_t size;
    char current[96];
    struct stat selected_metadata;
    (void)snprintf(selected_target, sizeof(selected_target), "generations/%s",
                   artifacts->bundle_digest);
    phase = "selected-alias-read";
    size = readlinkat(root, "selected", current, sizeof(current) - 1U);
    if (size >= 0) {
      current[(size_t)size] = '\0';
      if (strcmp(current, selected_target) != 0) selected_changed = 1;
    } else if (fstatat(root, "selected", &selected_metadata,
                       AT_SYMLINK_NOFOLLOW) == 0) {
      phase = "selected-alias-nonsymlink";
      status = CONFIT_ERR_GENERATION;
    } else if (errno == ENOENT) {
      selected_changed = 1;
    } else {
      phase = "selected-alias-stat";
      status = CONFIT_ERR_GENERATION;
    }
    if (status == CONFIT_OK && selected_changed) {
      (void)snprintf(selected_temp, sizeof(selected_temp), ".selected.%ld.%lu",
                     (long)getpid(), ++serial);
      phase = "selected-alias-symlink";
      if (symlinkat(selected_target, root, selected_temp) != 0) {
        status = CONFIT_ERR_GENERATION;
      } else {
        phase = "selected-alias-rename";
        if (renameat(root, selected_temp, root, "selected") != 0) {
          status = CONFIT_ERR_GENERATION;
        } else {
          phase = "selected-alias-sync";
          if (!confit_v4_posix_sync_dir(root)) status = CONFIT_ERR_GENERATION;
        }
      }
      if (status != CONFIT_OK) {
        (void)unlinkat(root, selected_temp, 0);
      }
    }
  }
  if (status == CONFIT_OK) {
    phase = "obsolete-generation-gc";
    if (!confit_v4_posix_gc_generations(generations, artifacts->bundle_digest)) {
      status = CONFIT_ERR_GENERATION;
    }
  }
  if (status != CONFIT_OK && diagnostic != 0 && diagnostic->message == 0) {
    confit_diagnostic_set(diagnostic, status, options->output_root, 0U, 0U,
                          phase);
  }
  if (out_changed_file_count != 0) {
    *out_changed_file_count = status == CONFIT_OK && (created || selected_changed) ? 1U : 0U;
  }
  if (generation >= 0) (void)close(generation);
  if (staging >= 0) (void)close(staging);
  if (staging_parent >= 0) (void)close(staging_parent);
  if (generations >= 0) (void)close(generations);
  if (lock_fd >= 0) (void)close(lock_fd);
  if (root >= 0) (void)close(root);
  return status;
}
#endif

ConfitStatus confit_v4_publish_artifacts(
    const ConfitV4PublishOptions *options, const ConfitV4ArtifactSet *artifacts,
    size_t *out_changed_file_count, ConfitDiagnostic *diagnostic) {
  ConfitV4NamedText texts[CONFIT_V4_PUBLISHED_TEXT_COUNT];
  ConfitStatus status;
  char generations[4096];
  char staging_parent[4096];
  char staging[4096];
  char generation[4096];
  char selected[4096];
  char selected_relative[128];
  int selected_changed = 0;
  int published_new = 0;
  int staging_exists = 0;

  if (options == 0 || options->output_root == 0 || options->output_root[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          "sealed publication requires an output root");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  status = confit_v4_complete_texts(artifacts, texts, diagnostic);
  if (status != CONFIT_OK) return status;
#if !defined(_WIN32)
  return confit_v4_publish_artifacts_posix(
      options, artifacts, texts, out_changed_file_count, diagnostic);
#endif
  status = confit_host_make_directories(options->output_root, diagnostic);
  if (status == CONFIT_OK) status = confit_v4_join_path(
      generations, sizeof(generations), options->output_root, "generations", diagnostic);
  if (status == CONFIT_OK) status = confit_host_make_directories(generations, diagnostic);
  if (status == CONFIT_OK) status = confit_v4_join_path(
      staging_parent, sizeof(staging_parent), options->output_root, ".staging", diagnostic);
  if (status == CONFIT_OK) status = confit_host_make_directories(staging_parent, diagnostic);
  if (status == CONFIT_OK) status = confit_v4_join_path(
      generation, sizeof(generation), generations, artifacts->bundle_digest, diagnostic);
  if (status != CONFIT_OK) return status;

  if (confit_v4_directory_exists(generation)) {
    status = confit_v4_verify_generation(generation, texts, diagnostic);
  } else {
    status = confit_v4_make_unique_staging(staging_parent, artifacts->bundle_digest,
                                           staging, sizeof(staging), diagnostic);
    if (status == CONFIT_OK) staging_exists = 1;
    if (status == CONFIT_OK) status = confit_v4_write_staging(
        staging, texts, options->fault_after_artifact, diagnostic);
    if (status == CONFIT_OK) {
      if (rename(staging, generation) != 0) {
        if (confit_v4_directory_exists(generation)) {
          status = confit_v4_verify_generation(generation, texts, diagnostic);
        } else {
          confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, generation,
                                0U, 0U, "failed to atomically publish sealed generation");
          status = CONFIT_ERR_GENERATION;
        }
      } else {
        staging_exists = 0;
        published_new = 1;
        confit_v4_mark_read_only(generation, texts);
      }
    }
    if (staging_exists != 0 && !confit_v4_remove_staging(staging, texts)) {
      if (status == CONFIT_OK) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, staging,
                              0U, 0U,
                              "failed to clean incomplete sealed-bundle staging");
        status = CONFIT_ERR_GENERATION;
      }
    }
  }
  if (status == CONFIT_OK) status = confit_v4_join_path(
      selected, sizeof(selected), options->output_root, "selected", diagnostic);
  if (status == CONFIT_OK &&
      snprintf(selected_relative, sizeof(selected_relative), "generations/%s",
               artifacts->bundle_digest) >= (int)sizeof(selected_relative)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, selected, 0U, 0U,
                          "selected alias path is too long");
    status = CONFIT_ERR_INTERNAL;
  }
  if (status == CONFIT_OK) status = confit_v4_publish_selected_alias(
      selected, selected_relative, &selected_changed, diagnostic);
  if (out_changed_file_count != 0) {
    *out_changed_file_count = status == CONFIT_OK &&
                              (selected_changed != 0 || published_new != 0)
                                  ? 1U
                                  : 0U;
  }
  return status;
}
