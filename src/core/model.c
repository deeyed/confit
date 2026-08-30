#include "confit/model.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct ConfitFragmentRecord {
  char *path;
  size_t parent_fragment;
  size_t source_ordinal;
} ConfitFragmentRecord;

typedef struct ConfitMenuRecord {
  size_t fragment;
  size_t parent_menu;
  char *prompt;
  char *help;
  char *declaration_path;
  size_t declaration_line;
  size_t declaration_column;
} ConfitMenuRecord;

typedef struct ConfitConfigRecord {
  size_t fragment;
  size_t menu;
  char *symbol;
  ConfitValueKind kind;
  char *prompt;
  char *help;
  ConfitValue default_value;
  int has_range;
  ConfitValue range_minimum;
  ConfitValue range_maximum;
  char **enum_values;
  size_t enum_value_count;
  char *dependency_text;
  char *choice_group;
  char *declaration_path;
  size_t declaration_line;
  size_t declaration_column;
} ConfitConfigRecord;

struct ConfitCatalog {
  ConfitAllocator allocator;
  char *mainmenu;
  ConfitFragmentRecord *fragments;
  size_t fragment_count;
  size_t fragment_capacity;
  ConfitMenuRecord *menus;
  size_t menu_count;
  size_t menu_capacity;
  ConfitConfigRecord *configs;
  size_t config_count;
  size_t config_capacity;
};

static const char kInvalidArgument[] = "invalid model argument";
static const char kInvalidAllocator[] = "allocator capability is incomplete";
static const char kOutOfMemory[] = "failed to allocate configuration model";
static const char kInvalidUtf8[] = "model text is not valid UTF-8";
static const char kInvalidControl[] = "model text contains a forbidden control";
static const char kTextTooLong[] = "model text exceeds its public limit";
static const char kInvalidSymbol[] = "configuration symbol is invalid";
static const char kInvalidKind[] = "configuration value kind is invalid";
static const char kHexTooLarge[] = "hex value exceeds the schema 6 domain";
static const char kInvalidEnumAtom[] = "enum atom is invalid";
static const char kInvalidEnumDomain[] = "enum domain is invalid";
static const char kDuplicateEnumAtom[] = "enum domain contains a duplicate atom";
static const char kInvalidChoice[] =
    "choice group requires a bool without depends_on and a bounded atom name";
static const char kBufferTooSmall[] = "canonical value buffer is too small";
static const char kCatalogLimit[] = "catalog exceeds a public resource limit";
static const char kDuplicatePath[] = "source fragment path is duplicated";
static const char kInvalidRelation[] = "catalog presentation relation is invalid";
static const char kDuplicateMenu[] = "source fragment already owns a menu";
static const char kDuplicateSymbol[] = "configuration symbol is duplicated";
static const char kInvalidRange[] = "configuration range invariant is invalid";
static const char kInvalidResolved[] = "resolved value invariant is invalid";

static void *confit_default_allocate(void *context, size_t size) {
  (void)context;
  return malloc(size);
}

static void confit_default_deallocate(void *context, void *pointer) {
  (void)context;
  free(pointer);
}

void confit_allocator_default(ConfitAllocator *out_allocator) {
  if (out_allocator == 0) {
    return;
  }
  out_allocator->context = 0;
  out_allocator->allocate = confit_default_allocate;
  out_allocator->deallocate = confit_default_deallocate;
}

int confit_allocator_is_valid(const ConfitAllocator *allocator) {
  return allocator != 0 && allocator->allocate != 0 &&
         allocator->deallocate != 0;
}

static int confit_allocator_resolve(const ConfitAllocator *requested,
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

static ConfitStatus confit_fail(ConfitDiagnostic *diagnostic,
                                ConfitStatus status, const char *message) {
  confit_diagnostic_set(diagnostic, status, 0, 0U, 0U, message);
  return status;
}

static int confit_size_add(size_t left, size_t right, size_t *out) {
  if (out == 0 || left > SIZE_MAX - right) {
    return 0;
  }
  *out = left + right;
  return 1;
}

static int confit_size_multiply(size_t left, size_t right, size_t *out) {
  if (out == 0 || (left != 0U && right > SIZE_MAX / left)) {
    return 0;
  }
  *out = left * right;
  return 1;
}

static int confit_bounded_length(const char *text, size_t limit,
                                 size_t *out_size) {
  size_t size;
  if (text == 0 || out_size == 0) {
    return 0;
  }
  for (size = 0U; size <= limit; ++size) {
    if (text[size] == '\0') {
      *out_size = size;
      return 1;
    }
  }
  return 0;
}

static int confit_utf8_continuation(unsigned char byte) {
  return (byte & 0xC0U) == 0x80U;
}

static int confit_text_validate_utf8(const char *text, size_t text_size) {
  const unsigned char *bytes = (const unsigned char *)text;
  size_t index = 0U;
  while (index < text_size) {
    const unsigned char first = bytes[index];
    size_t width;
    if (first < 0x80U) {
      width = 1U;
    } else if (first >= 0xC2U && first <= 0xDFU) {
      width = 2U;
    } else if (first >= 0xE0U && first <= 0xEFU) {
      width = 3U;
    } else if (first >= 0xF0U && first <= 0xF4U) {
      width = 4U;
    } else {
      return 0;
    }
    if (index + width > text_size ||
        (width >= 2U && !confit_utf8_continuation(bytes[index + 1U])) ||
        (width >= 3U && !confit_utf8_continuation(bytes[index + 2U])) ||
        (width >= 4U && !confit_utf8_continuation(bytes[index + 3U])) ||
        (width == 3U && first == 0xE0U && bytes[index + 1U] < 0xA0U) ||
        (width == 3U && first == 0xEDU && bytes[index + 1U] > 0x9FU) ||
        (width == 4U && first == 0xF0U && bytes[index + 1U] < 0x90U) ||
        (width == 4U && first == 0xF4U && bytes[index + 1U] > 0x8FU)) {
      return 0;
    }
    index += width;
  }
  return 1;
}

static int confit_text_has_forbidden_control(const char *text, size_t size,
                                             int allow_layout) {
  const unsigned char *bytes = (const unsigned char *)text;
  size_t index;
  for (index = 0U; index < size; ++index) {
    const unsigned char byte = bytes[index];
    if (byte == 0U || byte == 0x1BU || byte == 0x7FU) {
      return 1;
    }
    if (byte < 0x20U &&
        !(allow_layout && (byte == '\t' || byte == '\n' || byte == '\r'))) {
      return 1;
    }
    if (byte == 0xC2U && index + 1U < size && bytes[index + 1U] >= 0x80U &&
        bytes[index + 1U] <= 0x9FU) {
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_validate_text(const char *text, size_t size,
                                         size_t limit, int allow_empty,
                                         int allow_layout,
                                         ConfitDiagnostic *diagnostic) {
  if (text == 0 || (!allow_empty && size == 0U)) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidArgument);
  }
  if (size > limit) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kTextTooLong);
  }
  if (!confit_text_validate_utf8(text, size)) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidUtf8);
  }
  if (confit_text_has_forbidden_control(text, size, allow_layout)) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidControl);
  }
  return CONFIT_OK;
}

static ConfitStatus confit_copy_bytes(const char *text, size_t size,
                                      const ConfitAllocator *allocator,
                                      char **out,
                                      ConfitDiagnostic *diagnostic) {
  size_t allocation_size;
  char *copy;
  if (out == 0 || text == 0 ||
      !confit_size_add(size, 1U, &allocation_size)) {
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  copy = (char *)allocator->allocate(allocator->context, allocation_size);
  if (copy == 0) {
    return confit_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
  }
  if (size != 0U) {
    memcpy(copy, text, size);
  }
  copy[size] = '\0';
  *out = copy;
  return CONFIT_OK;
}

static ConfitStatus confit_copy_c_string(const char *text, size_t limit,
                                         int allow_empty, int allow_layout,
                                         const ConfitAllocator *allocator,
                                         char **out,
                                         ConfitDiagnostic *diagnostic) {
  size_t size;
  ConfitStatus status;
  if (!confit_bounded_length(text, limit, &size)) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kTextTooLong);
  }
  status = confit_validate_text(text, size, limit, allow_empty, allow_layout,
                                diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_copy_bytes(text, size, allocator, out, diagnostic);
}

void confit_value_init(ConfitValue *value) {
  if (value == 0) {
    return;
  }
  memset(value, 0, sizeof(*value));
  value->kind = CONFIT_VALUE_INVALID;
}

void confit_value_destroy(ConfitValue *value) {
  if (value == 0) {
    return;
  }
  if ((value->kind == CONFIT_VALUE_STRING || value->kind == CONFIT_VALUE_ENUM) &&
      value->data.text.data != 0 && confit_allocator_is_valid(&value->allocator)) {
    value->allocator.deallocate(value->allocator.context, value->data.text.data);
  }
  confit_value_init(value);
}

static ConfitStatus confit_value_set_scalar(ConfitValue *value,
                                            ConfitValueKind kind,
                                            int64_t signed_value,
                                            uint64_t unsigned_value,
                                            const ConfitAllocator *allocator,
                                            ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitValue candidate;
  if (value == 0) {
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  if (!confit_allocator_resolve(allocator, &resolved)) {
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidAllocator);
  }
  confit_value_init(&candidate);
  candidate.kind = kind;
  candidate.allocator = resolved;
  if (kind == CONFIT_VALUE_BOOL) {
    candidate.data.boolean = unsigned_value != 0U ? 1 : 0;
  } else if (kind == CONFIT_VALUE_INT) {
    candidate.data.integer = signed_value;
  } else if (kind == CONFIT_VALUE_HEX) {
    candidate.data.hexadecimal = unsigned_value;
  } else {
    return confit_fail(diagnostic, CONFIT_ERR_INTERNAL, kInvalidKind);
  }
  confit_value_destroy(value);
  *value = candidate;
  return CONFIT_OK;
}

ConfitStatus confit_value_set_bool(ConfitValue *value, int boolean,
                                   const ConfitAllocator *allocator,
                                   ConfitDiagnostic *diagnostic) {
  return confit_value_set_scalar(value, CONFIT_VALUE_BOOL,
                                 INT64_C(0),
                                 boolean != 0 ? UINT64_C(1) : UINT64_C(0),
                                 allocator, diagnostic);
}

ConfitStatus confit_value_set_int(ConfitValue *value, int64_t integer,
                                  const ConfitAllocator *allocator,
                                  ConfitDiagnostic *diagnostic) {
  return confit_value_set_scalar(value, CONFIT_VALUE_INT, integer, UINT64_C(0),
                                 allocator, diagnostic);
}

ConfitStatus confit_value_set_hex(ConfitValue *value, uint64_t hexadecimal,
                                  const ConfitAllocator *allocator,
                                  ConfitDiagnostic *diagnostic) {
  if (hexadecimal > (uint64_t)INT64_MAX) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kHexTooLarge);
  }
  return confit_value_set_scalar(value, CONFIT_VALUE_HEX, INT64_C(0),
                                 hexadecimal, allocator, diagnostic);
}

static int confit_enum_atom_is_valid_bytes(const char *atom, size_t size) {
  size_t index;
  if (atom == 0 || size == 0U || size > CONFIT_LIMIT_ENUM_ATOM_BYTES) {
    return 0;
  }
  for (index = 0U; index < size; ++index) {
    const unsigned char byte = (unsigned char)atom[index];
    if (!((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
          (byte >= '0' && byte <= '9') || byte == '_' || byte == '.' ||
          byte == '+' || byte == '-')) {
      return 0;
    }
  }
  return 1;
}

static ConfitStatus confit_value_set_text(ConfitValue *value,
                                          ConfitValueKind kind,
                                          const char *text, size_t text_size,
                                          const ConfitAllocator *allocator,
                                          ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitValue candidate;
  ConfitStatus status;
  if (value == 0 || text == 0) {
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  if (!confit_allocator_resolve(allocator, &resolved)) {
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidAllocator);
  }
  if (kind == CONFIT_VALUE_STRING) {
    status = confit_validate_text(text, text_size, CONFIT_LIMIT_STRING_BYTES, 1,
                                  1, diagnostic);
  } else if (kind == CONFIT_VALUE_ENUM) {
    status = confit_enum_atom_is_valid_bytes(text, text_size)
                 ? CONFIT_OK
                 : confit_fail(diagnostic, CONFIT_ERR_VALIDATION,
                               kInvalidEnumAtom);
  } else {
    status = confit_fail(diagnostic, CONFIT_ERR_INTERNAL, kInvalidKind);
  }
  if (status != CONFIT_OK) {
    return status;
  }
  confit_value_init(&candidate);
  candidate.kind = kind;
  candidate.allocator = resolved;
  status = confit_copy_bytes(text, text_size, &resolved,
                             &candidate.data.text.data, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  candidate.data.text.size = text_size;
  confit_value_destroy(value);
  *value = candidate;
  return CONFIT_OK;
}

ConfitStatus confit_value_set_string(ConfitValue *value, const char *text,
                                     size_t text_size,
                                     const ConfitAllocator *allocator,
                                     ConfitDiagnostic *diagnostic) {
  return confit_value_set_text(value, CONFIT_VALUE_STRING, text, text_size,
                               allocator, diagnostic);
}

ConfitStatus confit_value_set_enum(ConfitValue *value, const char *atom,
                                   size_t atom_size,
                                   const ConfitAllocator *allocator,
                                   ConfitDiagnostic *diagnostic) {
  return confit_value_set_text(value, CONFIT_VALUE_ENUM, atom, atom_size,
                               allocator, diagnostic);
}

ConfitStatus confit_value_copy(ConfitValue *destination,
                               const ConfitValue *source,
                               const ConfitAllocator *allocator,
                               ConfitDiagnostic *diagnostic) {
  ConfitValue candidate;
  ConfitStatus status;
  if (destination == 0 || source == 0) {
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  confit_value_init(&candidate);
  switch (source->kind) {
  case CONFIT_VALUE_BOOL:
    status = confit_value_set_bool(&candidate, source->data.boolean, allocator,
                                   diagnostic);
    break;
  case CONFIT_VALUE_INT:
    status = confit_value_set_int(&candidate, source->data.integer, allocator,
                                  diagnostic);
    break;
  case CONFIT_VALUE_HEX:
    status = confit_value_set_hex(&candidate, source->data.hexadecimal,
                                  allocator, diagnostic);
    break;
  case CONFIT_VALUE_STRING:
    status = confit_value_set_string(&candidate, source->data.text.data,
                                     source->data.text.size, allocator,
                                     diagnostic);
    break;
  case CONFIT_VALUE_ENUM:
    status = confit_value_set_enum(&candidate, source->data.text.data,
                                   source->data.text.size, allocator,
                                   diagnostic);
    break;
  case CONFIT_VALUE_INVALID:
  default:
    status = confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidKind);
    break;
  }
  if (status != CONFIT_OK) {
    confit_value_destroy(&candidate);
    return status;
  }
  confit_value_destroy(destination);
  *destination = candidate;
  return CONFIT_OK;
}

int confit_value_text(const ConfitValue *value, const char **out_text,
                      size_t *out_size) {
  if (value == 0 || out_text == 0 || out_size == 0 ||
      (value->kind != CONFIT_VALUE_STRING && value->kind != CONFIT_VALUE_ENUM)) {
    return 0;
  }
  *out_text = value->data.text.data;
  *out_size = value->data.text.size;
  return 1;
}

int confit_value_equal(const ConfitValue *left, const ConfitValue *right) {
  if (left == 0 || right == 0 || left->kind != right->kind) {
    return 0;
  }
  switch (left->kind) {
  case CONFIT_VALUE_BOOL:
    return (left->data.boolean != 0) == (right->data.boolean != 0);
  case CONFIT_VALUE_INT:
    return left->data.integer == right->data.integer;
  case CONFIT_VALUE_HEX:
    return left->data.hexadecimal == right->data.hexadecimal;
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
    return left->data.text.size == right->data.text.size &&
           (left->data.text.size == 0U ||
            memcmp(left->data.text.data, right->data.text.data,
                   left->data.text.size) == 0);
  case CONFIT_VALUE_INVALID:
  default:
    return 0;
  }
}

static size_t confit_unsigned_text(uint64_t value, unsigned int base,
                                   char output[32]) {
  static const char digits[] = "0123456789abcdef";
  char reverse[32];
  size_t count = 0U;
  size_t index;
  do {
    reverse[count++] = digits[value % base];
    value /= base;
  } while (value != 0U);
  for (index = 0U; index < count; ++index) {
    output[index] = reverse[count - index - 1U];
  }
  return count;
}

ConfitStatus confit_value_format_canonical(const ConfitValue *value,
                                            char *buffer,
                                            size_t buffer_size,
                                            size_t *out_size,
                                            ConfitDiagnostic *diagnostic) {
  const char *prefix;
  const char *payload = 0;
  char numeric[32];
  char length_text[32];
  size_t prefix_size;
  size_t payload_size;
  size_t length_size = 0U;
  size_t required;
  int length_framed = 0;
  if (value == 0 || out_size == 0) {
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    prefix = "bool:";
    payload = value->data.boolean != 0 ? "true" : "false";
    payload_size = value->data.boolean != 0 ? 4U : 5U;
    break;
  case CONFIT_VALUE_INT: {
    uint64_t magnitude;
    size_t offset = 0U;
    prefix = "int:";
    if (value->data.integer < 0) {
      numeric[offset++] = '-';
      magnitude = (uint64_t)(-(value->data.integer + 1)) + UINT64_C(1);
    } else {
      magnitude = (uint64_t)value->data.integer;
    }
    payload_size = offset + confit_unsigned_text(magnitude, 10U,
                                                  numeric + offset);
    payload = numeric;
    break;
  }
  case CONFIT_VALUE_HEX:
    prefix = "hex:0x";
    payload_size = confit_unsigned_text(value->data.hexadecimal, 16U, numeric);
    payload = numeric;
    break;
  case CONFIT_VALUE_STRING:
    if (value->data.text.data == 0) {
      return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidKind);
    }
    prefix = "string:";
    payload = value->data.text.data;
    payload_size = value->data.text.size;
    length_size = confit_unsigned_text((uint64_t)payload_size, 10U, length_text);
    length_framed = 1;
    break;
  case CONFIT_VALUE_ENUM:
    if (value->data.text.data == 0) {
      return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidKind);
    }
    prefix = "enum:";
    payload = value->data.text.data;
    payload_size = value->data.text.size;
    length_size = confit_unsigned_text((uint64_t)payload_size, 10U, length_text);
    length_framed = 1;
    break;
  case CONFIT_VALUE_INVALID:
  default:
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidKind);
  }
  prefix_size = strlen(prefix);
  required = prefix_size;
  if ((length_framed &&
       (!confit_size_add(required, length_size, &required) ||
        !confit_size_add(required, 1U, &required))) ||
      !confit_size_add(required, payload_size, &required)) {
    return confit_fail(diagnostic, CONFIT_ERR_INTERNAL, kTextTooLong);
  }
  *out_size = required;
  if (buffer == 0 || buffer_size <= required) {
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kBufferTooSmall);
  }
  memcpy(buffer, prefix, prefix_size);
  if (length_framed) {
    memcpy(buffer + prefix_size, length_text, length_size);
    buffer[prefix_size + length_size] = ':';
    memcpy(buffer + prefix_size + length_size + 1U, payload, payload_size);
  } else {
    memcpy(buffer + prefix_size, payload, payload_size);
  }
  buffer[required] = '\0';
  return CONFIT_OK;
}

int confit_symbol_is_valid(const char *symbol) {
  size_t size;
  size_t index;
  if (!confit_bounded_length(symbol, CONFIT_LIMIT_SYMBOL_BYTES, &size) || size == 0U ||
      symbol[0] < 'A' || symbol[0] > 'Z') {
    return 0;
  }
  for (index = 1U; index < size; ++index) {
    const unsigned char byte = (unsigned char)symbol[index];
    if (!((byte >= 'A' && byte <= 'Z') || (byte >= '0' && byte <= '9') ||
          byte == '_')) {
      return 0;
    }
  }
  return 1;
}

ConfitStatus confit_enum_domain_validate(const char *const *values,
                                         size_t value_count,
                                         ConfitDiagnostic *diagnostic) {
  size_t left;
  if (values == 0 || value_count == 0U ||
      value_count > CONFIT_LIMIT_ENUM_VALUES) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidEnumDomain);
  }
  for (left = 0U; left < value_count; ++left) {
    size_t atom_size;
    size_t right;
    if (!confit_bounded_length(values[left], CONFIT_LIMIT_ENUM_ATOM_BYTES,
                               &atom_size) ||
        !confit_enum_atom_is_valid_bytes(values[left], atom_size)) {
      return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidEnumAtom);
    }
    for (right = 0U; right < left; ++right) {
      if (strcmp(values[left], values[right]) == 0) {
        return confit_fail(diagnostic, CONFIT_ERR_VALIDATION,
                           kDuplicateEnumAtom);
      }
    }
  }
  return CONFIT_OK;
}

static void confit_fragment_record_destroy(ConfitFragmentRecord *record,
                                           const ConfitAllocator *allocator) {
  if (record != 0 && record->path != 0) {
    allocator->deallocate(allocator->context, record->path);
  }
  if (record != 0) {
    memset(record, 0, sizeof(*record));
  }
}

static void confit_menu_record_destroy(ConfitMenuRecord *record,
                                       const ConfitAllocator *allocator) {
  if (record == 0) {
    return;
  }
  if (record->prompt != 0) allocator->deallocate(allocator->context, record->prompt);
  if (record->help != 0) allocator->deallocate(allocator->context, record->help);
  if (record->declaration_path != 0)
    allocator->deallocate(allocator->context, record->declaration_path);
  memset(record, 0, sizeof(*record));
}

static void confit_config_record_destroy(ConfitConfigRecord *record,
                                         const ConfitAllocator *allocator) {
  size_t index;
  if (record == 0) {
    return;
  }
  if (record->symbol != 0) allocator->deallocate(allocator->context, record->symbol);
  if (record->prompt != 0) allocator->deallocate(allocator->context, record->prompt);
  if (record->help != 0) allocator->deallocate(allocator->context, record->help);
  if (record->dependency_text != 0)
    allocator->deallocate(allocator->context, record->dependency_text);
  if (record->choice_group != 0)
    allocator->deallocate(allocator->context, record->choice_group);
  if (record->declaration_path != 0)
    allocator->deallocate(allocator->context, record->declaration_path);
  confit_value_destroy(&record->default_value);
  confit_value_destroy(&record->range_minimum);
  confit_value_destroy(&record->range_maximum);
  for (index = 0U; index < record->enum_value_count; ++index) {
    if (record->enum_values[index] != 0)
      allocator->deallocate(allocator->context, record->enum_values[index]);
  }
  if (record->enum_values != 0)
    allocator->deallocate(allocator->context, record->enum_values);
  memset(record, 0, sizeof(*record));
}

static ConfitStatus confit_grow_array(void **storage, size_t *capacity,
                                      size_t count, size_t element_size,
                                      size_t limit,
                                      const ConfitAllocator *allocator,
                                      ConfitDiagnostic *diagnostic) {
  size_t allocation_size;
  size_t next_capacity;
  void *replacement;
  if (count < *capacity) {
    return CONFIT_OK;
  }
  if (count >= limit) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kCatalogLimit);
  }
  next_capacity = *capacity == 0U ? 4U : *capacity * 2U;
  if (next_capacity < *capacity || next_capacity > limit) {
    next_capacity = limit;
  }
  if (!confit_size_multiply(next_capacity, element_size, &allocation_size)) {
    return confit_fail(diagnostic, CONFIT_ERR_INTERNAL, kCatalogLimit);
  }
  replacement = allocator->allocate(allocator->context, allocation_size);
  if (replacement == 0) {
    return confit_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
  }
  memset(replacement, 0, allocation_size);
  if (*storage != 0 && count != 0U) {
    memcpy(replacement, *storage, count * element_size);
    allocator->deallocate(allocator->context, *storage);
  }
  *storage = replacement;
  *capacity = next_capacity;
  return CONFIT_OK;
}

ConfitStatus confit_catalog_create(const ConfitAllocator *allocator,
                                   ConfitCatalog **out_catalog,
                                   ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved;
  ConfitCatalog *catalog;
  if (out_catalog == 0) {
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  *out_catalog = 0;
  if (!confit_allocator_resolve(allocator, &resolved)) {
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidAllocator);
  }
  catalog = (ConfitCatalog *)resolved.allocate(resolved.context,
                                               sizeof(*catalog));
  if (catalog == 0) {
    return confit_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
  }
  memset(catalog, 0, sizeof(*catalog));
  catalog->allocator = resolved;
  *out_catalog = catalog;
  return CONFIT_OK;
}

void confit_catalog_reset(ConfitCatalog *catalog) {
  size_t index;
  if (catalog == 0) {
    return;
  }
  if (catalog->mainmenu != 0)
    catalog->allocator.deallocate(catalog->allocator.context, catalog->mainmenu);
  for (index = 0U; index < catalog->fragment_count; ++index)
    confit_fragment_record_destroy(&catalog->fragments[index], &catalog->allocator);
  for (index = 0U; index < catalog->menu_count; ++index)
    confit_menu_record_destroy(&catalog->menus[index], &catalog->allocator);
  for (index = 0U; index < catalog->config_count; ++index)
    confit_config_record_destroy(&catalog->configs[index], &catalog->allocator);
  if (catalog->fragments != 0)
    catalog->allocator.deallocate(catalog->allocator.context, catalog->fragments);
  if (catalog->menus != 0)
    catalog->allocator.deallocate(catalog->allocator.context, catalog->menus);
  if (catalog->configs != 0)
    catalog->allocator.deallocate(catalog->allocator.context, catalog->configs);
  catalog->mainmenu = 0;
  catalog->fragments = 0;
  catalog->fragment_count = 0U;
  catalog->fragment_capacity = 0U;
  catalog->menus = 0;
  catalog->menu_count = 0U;
  catalog->menu_capacity = 0U;
  catalog->configs = 0;
  catalog->config_count = 0U;
  catalog->config_capacity = 0U;
}

void confit_catalog_destroy(ConfitCatalog *catalog) {
  ConfitAllocator allocator;
  if (catalog == 0) {
    return;
  }
  allocator = catalog->allocator;
  confit_catalog_reset(catalog);
  allocator.deallocate(allocator.context, catalog);
}

ConfitStatus confit_catalog_set_mainmenu(ConfitCatalog *catalog,
                                         const char *mainmenu,
                                         ConfitDiagnostic *diagnostic) {
  char *copy = 0;
  ConfitStatus status;
  if (catalog == 0 || mainmenu == 0) {
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  status = confit_copy_c_string(mainmenu, CONFIT_LIMIT_PROMPT_BYTES, 0, 0,
                                &catalog->allocator, &copy, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  if (catalog->mainmenu != 0)
    catalog->allocator.deallocate(catalog->allocator.context, catalog->mainmenu);
  catalog->mainmenu = copy;
  return CONFIT_OK;
}

const char *confit_catalog_mainmenu(const ConfitCatalog *catalog) {
  return catalog != 0 ? catalog->mainmenu : 0;
}

ConfitStatus confit_catalog_add_fragment(
    ConfitCatalog *catalog, const ConfitSourceFragmentSpec *spec,
    size_t *out_index, ConfitDiagnostic *diagnostic) {
  ConfitFragmentRecord candidate;
  ConfitStatus status;
  size_t index;
  if (catalog == 0 || spec == 0 || spec->path == 0) {
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  if (catalog->fragment_count >= CONFIT_LIMIT_SOURCE_FRAGMENTS) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kCatalogLimit);
  }
  if (spec->parent_fragment != CONFIT_INDEX_NONE &&
      spec->parent_fragment >= catalog->fragment_count) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidRelation);
  }
  memset(&candidate, 0, sizeof(candidate));
  status = confit_copy_c_string(spec->path, CONFIT_LIMIT_SOURCE_PATH_BYTES, 0,
                                0, &catalog->allocator, &candidate.path,
                                diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  for (index = 0U; index < catalog->fragment_count; ++index) {
    if (strcmp(catalog->fragments[index].path, candidate.path) == 0) {
      confit_fragment_record_destroy(&candidate, &catalog->allocator);
      return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kDuplicatePath);
    }
  }
  candidate.parent_fragment = spec->parent_fragment;
  candidate.source_ordinal = spec->source_ordinal;
  status = confit_grow_array((void **)&catalog->fragments,
                             &catalog->fragment_capacity,
                             catalog->fragment_count,
                             sizeof(*catalog->fragments),
                             CONFIT_LIMIT_SOURCE_FRAGMENTS,
                             &catalog->allocator, diagnostic);
  if (status != CONFIT_OK) {
    confit_fragment_record_destroy(&candidate, &catalog->allocator);
    return status;
  }
  catalog->fragments[catalog->fragment_count] = candidate;
  if (out_index != 0) *out_index = catalog->fragment_count;
  catalog->fragment_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_copy_optional_span(const ConfitSourceSpan *span,
                                              const ConfitAllocator *allocator,
                                              char **out_path,
                                              ConfitDiagnostic *diagnostic) {
  if (span == 0 || span->path == 0) {
    *out_path = 0;
    return CONFIT_OK;
  }
  return confit_copy_c_string(span->path, CONFIT_LIMIT_SOURCE_PATH_BYTES, 0, 0,
                              allocator, out_path, diagnostic);
}

ConfitStatus confit_catalog_add_menu(ConfitCatalog *catalog,
                                     const ConfitMenuSpec *spec,
                                     size_t *out_index,
                                     ConfitDiagnostic *diagnostic) {
  ConfitMenuRecord candidate;
  ConfitStatus status;
  size_t depth;
  size_t index;
  size_t parent;
  if (catalog == 0 || spec == 0 || spec->fragment >= catalog->fragment_count ||
      (spec->parent_menu != CONFIT_INDEX_NONE &&
       spec->parent_menu >= catalog->menu_count)) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidRelation);
  }
  if (catalog->menu_count >= CONFIT_LIMIT_MENUS) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kCatalogLimit);
  }
  for (index = 0U; index < catalog->menu_count; ++index) {
    if (catalog->menus[index].fragment == spec->fragment) {
      return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kDuplicateMenu);
    }
  }
  depth = 1U;
  parent = spec->parent_menu;
  while (parent != CONFIT_INDEX_NONE) {
    depth += 1U;
    if (depth > CONFIT_LIMIT_VISIBLE_MENU_DEPTH) {
      return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidRelation);
    }
    parent = catalog->menus[parent].parent_menu;
  }
  memset(&candidate, 0, sizeof(candidate));
  candidate.fragment = spec->fragment;
  candidate.parent_menu = spec->parent_menu;
  candidate.declaration_line = spec->declaration.line;
  candidate.declaration_column = spec->declaration.column;
  status = confit_copy_c_string(spec->prompt, CONFIT_LIMIT_PROMPT_BYTES, 0, 0,
                                &catalog->allocator, &candidate.prompt,
                                diagnostic);
  if (status == CONFIT_OK)
    status = confit_copy_c_string(spec->help, CONFIT_LIMIT_HELP_BYTES, 0, 1,
                                  &catalog->allocator, &candidate.help,
                                  diagnostic);
  if (status == CONFIT_OK)
    status = confit_copy_optional_span(&spec->declaration, &catalog->allocator,
                                       &candidate.declaration_path, diagnostic);
  if (status != CONFIT_OK) {
    confit_menu_record_destroy(&candidate, &catalog->allocator);
    return status;
  }
  status = confit_grow_array((void **)&catalog->menus,
                             &catalog->menu_capacity, catalog->menu_count,
                             sizeof(*catalog->menus), CONFIT_LIMIT_MENUS,
                             &catalog->allocator, diagnostic);
  if (status != CONFIT_OK) {
    confit_menu_record_destroy(&candidate, &catalog->allocator);
    return status;
  }
  catalog->menus[catalog->menu_count] = candidate;
  if (out_index != 0) *out_index = catalog->menu_count;
  catalog->menu_count += 1U;
  return CONFIT_OK;
}

static int confit_value_compare_order(const ConfitValue *left,
                                      const ConfitValue *right, int *out) {
  if (left == 0 || right == 0 || out == 0 || left->kind != right->kind) {
    return 0;
  }
  if (left->kind == CONFIT_VALUE_INT) {
    *out = left->data.integer < right->data.integer
               ? -1
               : (left->data.integer > right->data.integer ? 1 : 0);
    return 1;
  }
  if (left->kind == CONFIT_VALUE_HEX) {
    *out = left->data.hexadecimal < right->data.hexadecimal
               ? -1
               : (left->data.hexadecimal > right->data.hexadecimal ? 1 : 0);
    return 1;
  }
  return 0;
}

static int confit_enum_domain_contains(const char *const *values, size_t count,
                                       const ConfitValue *value) {
  size_t index;
  if (value == 0 || value->kind != CONFIT_VALUE_ENUM) {
    return 0;
  }
  for (index = 0U; index < count; ++index) {
    const size_t size = strlen(values[index]);
    if (size == value->data.text.size &&
        memcmp(values[index], value->data.text.data, size) == 0) {
      return 1;
    }
  }
  return 0;
}

static int confit_choice_group_valid(const char *group) {
  size_t index;
  if (group == 0) return 1;
  if (group[0] == '\0' || strlen(group) >= CONFIT_LIMIT_CHOICE_GROUP_BYTES)
    return 0;
  for (index = 0U; group[index] != '\0'; ++index) {
    const unsigned char byte = (unsigned char)group[index];
    if (!((byte >= 'A' && byte <= 'Z') ||
          (byte >= 'a' && byte <= 'z') ||
          (byte >= '0' && byte <= '9') || byte == '_' || byte == '-' ||
          byte == '.'))
      return 0;
  }
  return 1;
}

static ConfitStatus confit_config_validate(const ConfitCatalog *catalog,
                                           const ConfitConfigSpec *spec,
                                           ConfitDiagnostic *diagnostic) {
  int comparison;
  size_t index;
  if (spec == 0 || spec->default_value == 0 ||
      spec->fragment >= catalog->fragment_count ||
      (spec->menu != CONFIT_INDEX_NONE && spec->menu >= catalog->menu_count)) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidRelation);
  }
  if (!confit_symbol_is_valid(spec->symbol)) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidSymbol);
  }
  if (spec->kind < CONFIT_VALUE_BOOL || spec->kind > CONFIT_VALUE_ENUM ||
      spec->default_value->kind != spec->kind) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidKind);
  }
  if (spec->choice_group != 0 &&
      (spec->kind != CONFIT_VALUE_BOOL || spec->dependency_text != 0 ||
       !confit_choice_group_valid(spec->choice_group)))
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidChoice);
  for (index = 0U; index < catalog->config_count; ++index) {
    if (strcmp(catalog->configs[index].symbol, spec->symbol) == 0) {
      return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kDuplicateSymbol);
    }
  }
  if (spec->range.present) {
    if ((spec->kind != CONFIT_VALUE_INT && spec->kind != CONFIT_VALUE_HEX) ||
        spec->range.minimum == 0 || spec->range.maximum == 0 ||
        !confit_value_compare_order(spec->range.minimum, spec->range.maximum,
                                    &comparison) ||
        comparison > 0 ||
        !confit_value_compare_order(spec->range.minimum, spec->default_value,
                                    &comparison) ||
        comparison > 0 ||
        !confit_value_compare_order(spec->default_value, spec->range.maximum,
                                    &comparison) ||
        comparison > 0) {
      return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidRange);
    }
  }
  if (spec->kind == CONFIT_VALUE_ENUM) {
    ConfitStatus status = confit_enum_domain_validate(
        spec->enum_values, spec->enum_value_count, diagnostic);
    if (status != CONFIT_OK) return status;
    if (!confit_enum_domain_contains(spec->enum_values,
                                     spec->enum_value_count,
                                     spec->default_value)) {
      return confit_fail(diagnostic, CONFIT_ERR_VALIDATION,
                         kInvalidEnumDomain);
    }
  } else if (spec->enum_values != 0 || spec->enum_value_count != 0U) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidEnumDomain);
  }
  return CONFIT_OK;
}

static ConfitStatus confit_config_record_build(
    ConfitConfigRecord *candidate, const ConfitConfigSpec *spec,
    const ConfitAllocator *allocator, ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  size_t allocation_size;
  size_t index;
  memset(candidate, 0, sizeof(*candidate));
  confit_value_init(&candidate->default_value);
  confit_value_init(&candidate->range_minimum);
  confit_value_init(&candidate->range_maximum);
  candidate->fragment = spec->fragment;
  candidate->menu = spec->menu;
  candidate->kind = spec->kind;
  candidate->declaration_line = spec->declaration.line;
  candidate->declaration_column = spec->declaration.column;
  status = confit_copy_c_string(spec->symbol, CONFIT_LIMIT_SYMBOL_BYTES, 0, 0, allocator,
                                &candidate->symbol, diagnostic);
  if (status == CONFIT_OK)
    status = confit_copy_c_string(spec->prompt, CONFIT_LIMIT_PROMPT_BYTES, 0, 0,
                                  allocator, &candidate->prompt, diagnostic);
  if (status == CONFIT_OK)
    status = confit_copy_c_string(spec->help, CONFIT_LIMIT_HELP_BYTES, 0, 1,
                                  allocator, &candidate->help, diagnostic);
  if (status == CONFIT_OK)
    status = confit_value_copy(&candidate->default_value, spec->default_value,
                               allocator, diagnostic);
  if (status == CONFIT_OK && spec->range.present) {
    status = confit_value_copy(&candidate->range_minimum, spec->range.minimum,
                               allocator, diagnostic);
    if (status == CONFIT_OK)
      status = confit_value_copy(&candidate->range_maximum,
                                 spec->range.maximum, allocator, diagnostic);
    if (status == CONFIT_OK) candidate->has_range = 1;
  }
  if (status == CONFIT_OK && spec->enum_value_count != 0U) {
    if (!confit_size_multiply(spec->enum_value_count,
                              sizeof(*candidate->enum_values),
                              &allocation_size)) {
      status = confit_fail(diagnostic, CONFIT_ERR_INTERNAL, kCatalogLimit);
    } else {
      candidate->enum_values = (char **)allocator->allocate(allocator->context,
                                                            allocation_size);
      if (candidate->enum_values == 0) {
        status = confit_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
      } else {
        memset(candidate->enum_values, 0, allocation_size);
        candidate->enum_value_count = spec->enum_value_count;
      }
    }
    for (index = 0U; status == CONFIT_OK && index < spec->enum_value_count;
         ++index) {
      status = confit_copy_c_string(spec->enum_values[index],
                                    CONFIT_LIMIT_ENUM_ATOM_BYTES, 0, 0,
                                    allocator, &candidate->enum_values[index],
                                    diagnostic);
    }
  }
  if (status == CONFIT_OK && spec->dependency_text != 0)
    status = confit_copy_c_string(spec->dependency_text,
                                  CONFIT_LIMIT_DEPENDENCY_TEXT_BYTES, 1, 0,
                                  allocator, &candidate->dependency_text,
                                  diagnostic);
  if (status == CONFIT_OK && spec->choice_group != 0)
    status = confit_copy_c_string(spec->choice_group,
                                  CONFIT_LIMIT_CHOICE_GROUP_BYTES, 0, 0,
                                  allocator, &candidate->choice_group,
                                  diagnostic);
  if (status == CONFIT_OK)
    status = confit_copy_optional_span(&spec->declaration, allocator,
                                       &candidate->declaration_path, diagnostic);
  if (status != CONFIT_OK) {
    confit_config_record_destroy(candidate, allocator);
  }
  return status;
}

ConfitStatus confit_catalog_add_config(ConfitCatalog *catalog,
                                       const ConfitConfigSpec *spec,
                                       size_t *out_index,
                                       ConfitDiagnostic *diagnostic) {
  ConfitConfigRecord candidate;
  ConfitStatus status;
  if (catalog == 0) {
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  }
  if (catalog->config_count >= CONFIT_LIMIT_CONFIG_SYMBOLS) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kCatalogLimit);
  }
  status = confit_config_validate(catalog, spec, diagnostic);
  if (status != CONFIT_OK) return status;
  status = confit_config_record_build(&candidate, spec, &catalog->allocator,
                                      diagnostic);
  if (status != CONFIT_OK) return status;
  status = confit_grow_array((void **)&catalog->configs,
                             &catalog->config_capacity, catalog->config_count,
                             sizeof(*catalog->configs),
                             CONFIT_LIMIT_CONFIG_SYMBOLS,
                             &catalog->allocator, diagnostic);
  if (status != CONFIT_OK) {
    confit_config_record_destroy(&candidate, &catalog->allocator);
    return status;
  }
  catalog->configs[catalog->config_count] = candidate;
  if (out_index != 0) *out_index = catalog->config_count;
  catalog->config_count += 1U;
  return CONFIT_OK;
}

size_t confit_catalog_fragment_count(const ConfitCatalog *catalog) {
  return catalog != 0 ? catalog->fragment_count : 0U;
}

size_t confit_catalog_menu_count(const ConfitCatalog *catalog) {
  return catalog != 0 ? catalog->menu_count : 0U;
}

size_t confit_catalog_config_count(const ConfitCatalog *catalog) {
  return catalog != 0 ? catalog->config_count : 0U;
}

int confit_catalog_fragment_at(const ConfitCatalog *catalog, size_t index,
                               ConfitSourceFragmentView *out_view) {
  if (catalog == 0 || out_view == 0 || index >= catalog->fragment_count) return 0;
  out_view->path = catalog->fragments[index].path;
  out_view->parent_fragment = catalog->fragments[index].parent_fragment;
  out_view->source_ordinal = catalog->fragments[index].source_ordinal;
  return 1;
}

int confit_catalog_menu_at(const ConfitCatalog *catalog, size_t index,
                           ConfitMenuView *out_view) {
  if (catalog == 0 || out_view == 0 || index >= catalog->menu_count) return 0;
  out_view->fragment = catalog->menus[index].fragment;
  out_view->parent_menu = catalog->menus[index].parent_menu;
  out_view->prompt = catalog->menus[index].prompt;
  out_view->help = catalog->menus[index].help;
  out_view->declaration.path = catalog->menus[index].declaration_path;
  out_view->declaration.line = catalog->menus[index].declaration_line;
  out_view->declaration.column = catalog->menus[index].declaration_column;
  return 1;
}

int confit_catalog_config_at(const ConfitCatalog *catalog, size_t index,
                             ConfitConfigView *out_view) {
  const ConfitConfigRecord *record;
  if (catalog == 0 || out_view == 0 || index >= catalog->config_count) return 0;
  record = &catalog->configs[index];
  out_view->fragment = record->fragment;
  out_view->menu = record->menu;
  out_view->symbol = record->symbol;
  out_view->kind = record->kind;
  out_view->prompt = record->prompt;
  out_view->help = record->help;
  out_view->default_value = &record->default_value;
  out_view->has_range = record->has_range;
  out_view->range_minimum = record->has_range ? &record->range_minimum : 0;
  out_view->range_maximum = record->has_range ? &record->range_maximum : 0;
  out_view->enum_values = (const char *const *)record->enum_values;
  out_view->enum_value_count = record->enum_value_count;
  out_view->dependency_text = record->dependency_text;
  out_view->choice_group = record->choice_group;
  out_view->declaration.path = record->declaration_path;
  out_view->declaration.line = record->declaration_line;
  out_view->declaration.column = record->declaration_column;
  return 1;
}

int confit_catalog_find_config(const ConfitCatalog *catalog,
                               const char *symbol,
                               ConfitConfigView *out_view) {
  size_t index;
  if (catalog == 0 || symbol == 0 || out_view == 0) return 0;
  for (index = 0U; index < catalog->config_count; ++index) {
    if (strcmp(catalog->configs[index].symbol, symbol) == 0) {
      return confit_catalog_config_at(catalog, index, out_view);
    }
  }
  return 0;
}

void confit_assignment_init(ConfitAssignment *assignment) {
  if (assignment == 0) return;
  memset(assignment, 0, sizeof(*assignment));
  confit_value_init(&assignment->value);
}

void confit_assignment_destroy(ConfitAssignment *assignment) {
  if (assignment == 0) return;
  if (assignment->symbol != 0 && confit_allocator_is_valid(&assignment->allocator))
    assignment->allocator.deallocate(assignment->allocator.context,
                                     assignment->symbol);
  confit_value_destroy(&assignment->value);
  confit_assignment_init(assignment);
}

ConfitStatus confit_assignment_set(ConfitAssignment *assignment,
                                   const char *symbol,
                                   const ConfitValue *value,
                                   const ConfitAllocator *allocator,
                                   ConfitDiagnostic *diagnostic) {
  ConfitAssignment candidate;
  ConfitStatus status;
  if (assignment == 0 || value == 0 || !confit_symbol_is_valid(symbol))
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidSymbol);
  confit_assignment_init(&candidate);
  if (!confit_allocator_resolve(allocator, &candidate.allocator))
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidAllocator);
  status = confit_copy_c_string(symbol, CONFIT_LIMIT_SYMBOL_BYTES, 0, 0, &candidate.allocator,
                                &candidate.symbol, diagnostic);
  if (status == CONFIT_OK)
    status = confit_value_copy(&candidate.value, value, &candidate.allocator,
                               diagnostic);
  if (status != CONFIT_OK) {
    confit_assignment_destroy(&candidate);
    return status;
  }
  confit_assignment_destroy(assignment);
  *assignment = candidate;
  return CONFIT_OK;
}

void confit_reason_node_init(ConfitReasonNode *reason) {
  if (reason == 0) return;
  memset(reason, 0, sizeof(*reason));
  reason->kind = CONFIT_REASON_NONE;
}

void confit_reason_node_destroy(ConfitReasonNode *reason) {
  if (reason == 0) return;
  if (confit_allocator_is_valid(&reason->allocator)) {
    if (reason->subject_symbol != 0)
      reason->allocator.deallocate(reason->allocator.context,
                                   reason->subject_symbol);
    if (reason->related_symbol != 0)
      reason->allocator.deallocate(reason->allocator.context,
                                   reason->related_symbol);
    if (reason->detail != 0)
      reason->allocator.deallocate(reason->allocator.context, reason->detail);
  }
  confit_reason_node_init(reason);
}

ConfitStatus confit_reason_node_set(
    ConfitReasonNode *reason, ConfitReasonKind kind, int result,
    const char *subject_symbol, const char *related_symbol,
    const char *detail, const size_t *children, size_t child_count,
    const ConfitAllocator *allocator, ConfitDiagnostic *diagnostic) {
  ConfitReasonNode candidate;
  ConfitStatus status = CONFIT_OK;
  size_t index;
  if (reason == 0 || kind < CONFIT_REASON_NONE ||
      kind > CONFIT_REASON_UNAVAILABLE ||
      child_count > CONFIT_REASON_CHILD_LIMIT ||
      (child_count != 0U && children == 0) ||
      (subject_symbol != 0 && !confit_symbol_is_valid(subject_symbol)) ||
      (related_symbol != 0 && !confit_symbol_is_valid(related_symbol))) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidArgument);
  }
  confit_reason_node_init(&candidate);
  if (!confit_allocator_resolve(allocator, &candidate.allocator))
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidAllocator);
  candidate.kind = kind;
  candidate.result = result != 0;
  candidate.child_count = child_count;
  for (index = 0U; index < child_count; ++index) candidate.children[index] = children[index];
  if (subject_symbol != 0)
    status = confit_copy_c_string(subject_symbol, CONFIT_LIMIT_SYMBOL_BYTES, 0, 0,
                                  &candidate.allocator,
                                  &candidate.subject_symbol, diagnostic);
  if (status == CONFIT_OK && related_symbol != 0)
    status = confit_copy_c_string(related_symbol, CONFIT_LIMIT_SYMBOL_BYTES, 0, 0,
                                  &candidate.allocator,
                                  &candidate.related_symbol, diagnostic);
  if (status == CONFIT_OK && detail != 0)
    status = confit_copy_c_string(detail, CONFIT_LIMIT_DEPENDENCY_TEXT_BYTES,
                                  1, 1, &candidate.allocator,
                                  &candidate.detail, diagnostic);
  if (status != CONFIT_OK) {
    confit_reason_node_destroy(&candidate);
    return status;
  }
  confit_reason_node_destroy(reason);
  *reason = candidate;
  return CONFIT_OK;
}

void confit_resolved_value_init(ConfitResolvedValue *resolved) {
  if (resolved == 0) return;
  memset(resolved, 0, sizeof(*resolved));
  confit_value_init(&resolved->default_value);
  confit_value_init(&resolved->effective_value);
  resolved->origin = CONFIT_ORIGIN_INVALID;
  resolved->reason = CONFIT_INDEX_NONE;
}

void confit_resolved_value_destroy(ConfitResolvedValue *resolved) {
  if (resolved == 0) return;
  if (resolved->symbol != 0 && confit_allocator_is_valid(&resolved->allocator))
    resolved->allocator.deallocate(resolved->allocator.context,
                                   resolved->symbol);
  confit_value_destroy(&resolved->default_value);
  confit_value_destroy(&resolved->effective_value);
  confit_resolved_value_init(resolved);
}

ConfitStatus confit_resolved_value_set(
    ConfitResolvedValue *resolved, const char *symbol,
    const ConfitValue *default_value, const ConfitValue *effective_value,
    ConfitValueOrigin origin, int available, size_t reason,
    const ConfitAllocator *allocator, ConfitDiagnostic *diagnostic) {
  ConfitResolvedValue candidate;
  ConfitStatus status;
  if (resolved == 0 || !confit_symbol_is_valid(symbol) || default_value == 0 ||
      effective_value == 0 || default_value->kind != effective_value->kind ||
      (origin != CONFIT_ORIGIN_DEFAULT && origin != CONFIT_ORIGIN_USER)) {
    return confit_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidResolved);
  }
  confit_resolved_value_init(&candidate);
  if (!confit_allocator_resolve(allocator, &candidate.allocator))
    return confit_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidAllocator);
  candidate.origin = origin;
  candidate.available = available != 0 ? 1 : 0;
  candidate.reason = reason;
  status = confit_copy_c_string(symbol, CONFIT_LIMIT_SYMBOL_BYTES, 0, 0, &candidate.allocator,
                                &candidate.symbol, diagnostic);
  if (status == CONFIT_OK)
    status = confit_value_copy(&candidate.default_value, default_value,
                               &candidate.allocator, diagnostic);
  if (status == CONFIT_OK)
    status = confit_value_copy(&candidate.effective_value, effective_value,
                               &candidate.allocator, diagnostic);
  if (status != CONFIT_OK) {
    confit_resolved_value_destroy(&candidate);
    return status;
  }
  confit_resolved_value_destroy(resolved);
  *resolved = candidate;
  return CONFIT_OK;
}
