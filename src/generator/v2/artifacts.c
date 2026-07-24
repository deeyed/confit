#include "confit/generator_v2.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/version.h"

typedef struct ConfitV2ArtifactBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitV2ArtifactBuilder;

static const char kInvalidArgument[] = "invalid schema v2 artifact argument";
static const char kAllocationFailed[] =
    "failed to allocate schema v2 artifact text";
static const char kHeaderListUnsupported[] =
    "schema v2 header artifact needs an explicit list encoding";

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

static int confit_v2_is_separator(char value) {
  return value == '/' || value == '\\';
}

static int confit_v2_is_identifier_char(char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9');
}

static int confit_v2_is_artifact_name(const char *text) {
  size_t index;

  if (text == 0 || text[0] == '\0') {
    return 0;
  }
  for (index = 0U; text[index] != '\0'; ++index) {
    const char value = text[index];

    if (!confit_v2_is_identifier_char(value) && value != '_' && value != '-') {
      return 0;
    }
  }
  return 1;
}

static char confit_v2_to_upper(char value) {
  return value >= 'a' && value <= 'z' ? (char)(value - 'a' + 'A') : value;
}

static const char *confit_v2_option_type_name(ConfitV2OptionType type) {
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

static const char *confit_v2_write_domain_name(ConfitV2WriteDomain domain) {
  switch (domain) {
  case CONFIT_V2_WRITE_DOMAIN_SCHEMA:
    return "schema";
  case CONFIT_V2_WRITE_DOMAIN_PROFILE:
    return "profile";
  case CONFIT_V2_WRITE_DOMAIN_TARGET:
    return "target";
  case CONFIT_V2_WRITE_DOMAIN_COMPUTED:
    return "computed";
  case CONFIT_V2_WRITE_DOMAIN_INVALID:
  default:
    return "invalid";
  }
}

static const char *confit_v2_assignment_origin_name(
    ConfitV2AssignmentOrigin origin) {
  switch (origin) {
  case CONFIT_V2_ASSIGNMENT_ORIGIN_SCHEMA_DEFAULT:
    return "schema_default";
  case CONFIT_V2_ASSIGNMENT_ORIGIN_PROFILE:
    return "profile";
  case CONFIT_V2_ASSIGNMENT_ORIGIN_TARGET:
    return "target";
  case CONFIT_V2_ASSIGNMENT_ORIGIN_USER:
    return "user";
  case CONFIT_V2_ASSIGNMENT_ORIGIN_UNSET:
  default:
    return "unset";
  }
}

static const char *confit_v2_effective_origin_name(
    ConfitV2EffectiveValueOrigin origin) {
  switch (origin) {
  case CONFIT_V2_EFFECTIVE_VALUE_REQUESTED:
    return "requested";
  case CONFIT_V2_EFFECTIVE_VALUE_CONDITIONAL_DEFAULT:
    return "conditional_default";
  case CONFIT_V2_EFFECTIVE_VALUE_DEFAULT:
    return "default";
  case CONFIT_V2_EFFECTIVE_VALUE_COMPUTED:
    return "computed";
  case CONFIT_V2_EFFECTIVE_VALUE_UNSET:
  default:
    return "unset";
  }
}

static const char *confit_v2_constraint_outcome_name(
    ConfitV2ConstraintOutcome outcome) {
  switch (outcome) {
  case CONFIT_V2_CONSTRAINT_NOT_APPLICABLE:
    return "not_applicable";
  case CONFIT_V2_CONSTRAINT_PASSED:
    return "passed";
  case CONFIT_V2_CONSTRAINT_FAILED:
  default:
    return "failed";
  }
}

static const char *confit_v2_provenance_kind_name(ConfitV2ProvenanceKind kind) {
  switch (kind) {
  case CONFIT_V2_PROVENANCE_SCHEMA_DEFAULT:
    return "schema_default";
  case CONFIT_V2_PROVENANCE_CONDITIONAL_DEFAULT:
    return "conditional_default";
  case CONFIT_V2_PROVENANCE_PROFILE_ASSIGNMENT:
    return "profile_assignment";
  case CONFIT_V2_PROVENANCE_TARGET_ASSIGNMENT:
    return "target_assignment";
  case CONFIT_V2_PROVENANCE_USER_ASSIGNMENT:
    return "user_assignment";
  case CONFIT_V2_PROVENANCE_COMPUTED:
    return "computed";
  case CONFIT_V2_PROVENANCE_CHOICE_DECISION:
    return "choice_decision";
  case CONFIT_V2_PROVENANCE_CONSTRAINT:
    return "constraint";
  case CONFIT_V2_PROVENANCE_UNSET:
    return "unset";
  case CONFIT_V2_PROVENANCE_EFFECTIVE_VALUE:
  default:
    return "effective_value";
  }
}

static const char *confit_v2_invalidation_kind_name(
    ConfitV2InvalidationKind kind) {
  switch (kind) {
  case CONFIT_V2_INVALIDATION_OPTION:
    return "option";
  case CONFIT_V2_INVALIDATION_CHOICE:
    return "choice";
  case CONFIT_V2_INVALIDATION_CONSTRAINT:
  default:
    return "constraint";
  }
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

static ConfitStatus confit_v2_append_lua_string(ConfitV2ArtifactBuilder *builder,
                                                 const char *text) {
  size_t index;
  ConfitStatus status = confit_v2_builder_append_char(builder, '"');

  /* Lua does not accept JSON's \uXXXX escapes. Keep the QSM module valid for
   * every control byte that a schema string can carry. */
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
      status = confit_v2_builder_appendf(builder, "\\%03u", (unsigned)value);
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

static ConfitStatus confit_v2_append_cmake_string(ConfitV2ArtifactBuilder *builder,
                                                   const char *text) {
  size_t index;
  ConfitStatus status = confit_v2_builder_append_char(builder, '"');

  for (index = 0U; status == CONFIT_OK && text != 0 && text[index] != '\0';
       ++index) {
    const char value = text[index];
    if (value == '"' || value == '\\' || value == '$' || value == ';') {
      status = confit_v2_builder_append_char(builder, '\\');
      if (status == CONFIT_OK) {
        status = confit_v2_builder_append_char(builder, value);
      }
    } else if (value == '\n') {
      status = confit_v2_builder_append(builder, "\\n");
    } else if (value == '\r') {
      status = confit_v2_builder_append(builder, "\\r");
    } else if (value == '\t') {
      status = confit_v2_builder_append(builder, "\\t");
    } else {
      status = confit_v2_builder_append_char(builder, value);
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

static ConfitStatus confit_v2_append_lua_value(ConfitV2ArtifactBuilder *builder,
                                                const ConfitV2Value *value,
                                                ConfitV2OptionType type) {
  size_t index;

  if (value == 0 || value->kind == CONFIT_V2_VALUE_UNSET) {
    return confit_v2_builder_append(builder, "nil");
  }
  if (value->kind == CONFIT_V2_VALUE_STRING_LIST) {
    ConfitStatus status = confit_v2_builder_append(builder, "{");
    for (index = 0U; status == CONFIT_OK && index < value->as.string_list.count;
         ++index) {
      if (index != 0U) {
        status = confit_v2_builder_append(builder, ", ");
      }
      if (status == CONFIT_OK) {
        status = confit_v2_append_lua_string(builder,
                                              value->as.string_list.items[index]);
      }
    }
    if (status == CONFIT_OK) {
      status = confit_v2_builder_append(builder, "}");
    }
    return status;
  }
  if (type == CONFIT_V2_OPTION_TYPE_HEX && value->kind == CONFIT_V2_VALUE_UINT) {
    return confit_v2_builder_appendf(builder, "%llu",
                                     (unsigned long long)value->as.uint_value);
  }
  return confit_v2_append_json_value(builder, value, type);
}

static ConfitStatus confit_v2_append_cmake_value(ConfitV2ArtifactBuilder *builder,
                                                  const ConfitV2Value *value,
                                                  ConfitV2OptionType type) {
  ConfitV2ArtifactBuilder text;
  ConfitStatus status;
  size_t index;

  if (value == 0 || value->kind == CONFIT_V2_VALUE_UNSET) {
    return confit_v2_builder_append(builder, "\"\"");
  }
  if (value->kind == CONFIT_V2_VALUE_BOOL) {
    return confit_v2_builder_append(builder,
                                    value->as.bool_value ? "\"ON\"" : "\"OFF\"");
  }
  if (value->kind == CONFIT_V2_VALUE_TRISTATE) {
    return confit_v2_builder_appendf(builder, "\"%c\"", value->as.tristate_value);
  }
  confit_v2_builder_init(&text);
  if (value->kind == CONFIT_V2_VALUE_STRING_LIST) {
    status = confit_v2_builder_append_char(builder, '"');
    for (index = 0U; status == CONFIT_OK &&
                     index < value->as.string_list.count;
         ++index) {
      const char *item = value->as.string_list.items[index];
      size_t item_index;

      if (index != 0U) {
        status = confit_v2_builder_append_char(builder, ';');
      }
      for (item_index = 0U; status == CONFIT_OK && item != 0 &&
                             item[item_index] != '\0';
           ++item_index) {
        const char character = item[item_index];

        if (character == '"' || character == '\\' || character == '$' ||
            character == ';') {
          status = confit_v2_builder_append_char(builder, '\\');
        }
        if (status == CONFIT_OK) {
          status = confit_v2_builder_append_char(builder, character);
        }
      }
    }
    if (status == CONFIT_OK) {
      status = confit_v2_builder_append_char(builder, '"');
    }
    confit_v2_builder_clear(&text);
    return status;
  } else if (type == CONFIT_V2_OPTION_TYPE_HEX &&
             value->kind == CONFIT_V2_VALUE_UINT) {
    status = confit_v2_append_hex(&text, value->as.uint_value);
  } else if (value->kind == CONFIT_V2_VALUE_INT) {
    status = confit_v2_builder_appendf(&text, "%lld",
                                       (long long)value->as.int_value);
  } else if (value->kind == CONFIT_V2_VALUE_UINT) {
    status = confit_v2_builder_appendf(&text, "%llu",
                                       (unsigned long long)value->as.uint_value);
  } else if (value->kind == CONFIT_V2_VALUE_FLOAT) {
    status = confit_v2_builder_appendf(&text, "%.17g", value->as.float_value);
  } else {
    status = confit_v2_builder_append(&text, value->as.string_value);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_append_cmake_string(builder, text.text);
  }
  confit_v2_builder_clear(&text);
  return status;
}

static const char *confit_v2_source_label(const ConfitV2Snapshot *snapshot,
                                          const char *source) {
  const char *root = confit_v2_snapshot_source_root(snapshot);
  size_t root_size;
  const char *cursor;

  if (source == 0 || source[0] == '\0') {
    return "";
  }
  root_size = root != 0 ? strlen(root) : 0U;
  if (root_size > 0U && strncmp(root, source, root_size) == 0 &&
      confit_v2_is_separator(source[root_size])) {
    return source + root_size + 1U;
  }
  if (!confit_v2_is_separator(source[0]) &&
      !(source[0] != '\0' && source[1] == ':')) {
    return source;
  }
  cursor = source + strlen(source);
  while (cursor > source && !confit_v2_is_separator(cursor[-1])) {
    --cursor;
  }
  return cursor;
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

static ConfitStatus confit_v2_append_identity_json(
    ConfitV2ArtifactBuilder *builder, const ConfitV2Snapshot *snapshot,
    const char *schema, int trailing_comma) {
  ConfitStatus status;

  status = confit_v2_builder_append(builder, "  \"schema\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(builder, schema);
  if (status == CONFIT_OK) status = confit_v2_builder_append(builder, ",\n  \"schema_version\": 2,\n  \"resolver_abi\": \"confit-resolver-v2\",\n  \"artifact_abi\": \"confit-artifact-v2\",\n  \"confit_version\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(builder, CONFIT_VERSION_RELEASE);
  if (status == CONFIT_OK) status = confit_v2_builder_append(builder, ",\n  \"project\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(builder, confit_v2_snapshot_project_name(snapshot));
  if (status == CONFIT_OK) status = confit_v2_builder_append(builder, ",\n  \"profile\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(builder, confit_v2_snapshot_profile_name(snapshot));
  if (status == CONFIT_OK) status = confit_v2_builder_append(builder, ",\n  \"target\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(builder, confit_v2_snapshot_target_name(snapshot));
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(builder, ",\n  \"source_hash\": \"0x%016llX\",\n  \"input_hash\": \"0x%016llX\",\n  \"snapshot_hash\": \"0x%016llX\"%s\n",
      (unsigned long long)confit_v2_snapshot_source_hash(snapshot),
      (unsigned long long)confit_v2_snapshot_input_hash(snapshot),
      (unsigned long long)confit_v2_snapshot_semantic_hash(snapshot),
      trailing_comma ? "," : "");
  return status;
}

static ConfitStatus confit_v2_generate_header(const ConfitV2Snapshot *snapshot,
                                               char **out, ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactBuilder builder;
  char *guard = 0;
  size_t index;
  ConfitStatus status;

  confit_v2_builder_init(&builder);
  status = confit_v2_make_option_macro(snapshot, "header", &guard);
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, "#ifndef %s_H\n#define %s_H\n\n", guard, guard);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "#define CONFIT_SCHEMA_VERSION 2\n#define CONFIT_RESOLVER_ABI 2\n#define CONFIT_ARTIFACT_ABI \"confit-artifact-v2\"\n");
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

static ConfitStatus confit_v2_append_requested_json(ConfitV2ArtifactBuilder *builder, const ConfitV2Snapshot *snapshot, const ConfitV2SnapshotOption *option) {
  const ConfitV2SnapshotAssignment *requested = &option->requested;
  ConfitStatus status = confit_v2_builder_append(builder, "{ \"state\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(builder, !requested->is_present ? "absent" : requested->is_unset ? "unset" : "set");
  if (status == CONFIT_OK && requested->is_present) status = confit_v2_builder_append(builder, ", \"origin\": ");
  if (status == CONFIT_OK && requested->is_present) status = confit_v2_append_json_string(builder, confit_v2_assignment_origin_name(requested->origin));
  if (status == CONFIT_OK && requested->is_set) status = confit_v2_builder_append(builder, ", \"value\": ");
  if (status == CONFIT_OK && requested->is_set) status = confit_v2_append_json_value(builder, &requested->value, option->type);
  if (status == CONFIT_OK && requested->is_present) status = confit_v2_builder_append(builder, ", \"source\": ");
  if (status == CONFIT_OK && requested->is_present) status = confit_v2_append_json_string(builder, confit_v2_source_label(snapshot, requested->source_path));
  if (status == CONFIT_OK) status = confit_v2_builder_append(builder, " }");
  return status;
}

static ConfitStatus confit_v2_generate_report(const ConfitV2Snapshot *snapshot,
                                               char **out) {
  ConfitV2ArtifactBuilder builder;
  size_t index;
  ConfitStatus status;

  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n");
  if (status == CONFIT_OK) status = confit_v2_append_identity_json(&builder, snapshot, "confit-report-v2", 1);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  \"options\": [\n");
  for (index = 0U; status == CONFIT_OK && index < confit_v2_snapshot_option_count(snapshot); ++index) {
    const ConfitV2SnapshotOption *option = confit_v2_snapshot_option_at(snapshot, index);
    status = confit_v2_builder_append(&builder, "    { \"id\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, option->id);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"type\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, confit_v2_option_type_name(option->type));
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"write_domain\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, confit_v2_write_domain_name(option->write_domain));
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"requested\": ");
    if (status == CONFIT_OK) status = confit_v2_append_requested_json(&builder, snapshot, option);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"effective\": { \"state\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, option->effective_is_set ? "set" : "unset");
    if (status == CONFIT_OK && option->effective_is_set) status = confit_v2_builder_append(&builder, ", \"value\": ");
    if (status == CONFIT_OK && option->effective_is_set) status = confit_v2_append_json_value(&builder, &option->effective_value, option->type);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"origin\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, confit_v2_effective_origin_name(option->effective_origin));
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"source\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, confit_v2_source_label(snapshot, option->effective_source_path));
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, " }, \"available\": %s, \"visible\": %s }%s\n", option->available ? "true" : "false", option->visible ? "true" : "false", index + 1U == confit_v2_snapshot_option_count(snapshot) ? "" : ",");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  ],\n  \"choices\": [\n");
  for (index = 0U; status == CONFIT_OK && index < confit_v2_snapshot_choice_count(snapshot); ++index) {
    const ConfitV2SnapshotChoice *choice = confit_v2_snapshot_choice_at(snapshot, index);
    status = confit_v2_builder_append(&builder, "    { \"id\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, choice->id);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, ", \"available\": %s, \"visible\": %s, \"selected_member\": ", choice->available ? "true" : "false", choice->visible ? "true" : "false");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, choice->selected_member_id);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, ", \"effective_member_count\": %llu }%s\n", (unsigned long long)choice->effective_member_count, index + 1U == confit_v2_snapshot_choice_count(snapshot) ? "" : ",");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  ],\n  \"constraints\": [\n");
  for (index = 0U; status == CONFIT_OK && index < confit_v2_snapshot_constraint_count(snapshot); ++index) {
    const ConfitV2SnapshotConstraint *constraint = confit_v2_snapshot_constraint_at(snapshot, index);
    status = confit_v2_builder_append(&builder, "    { \"id\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, constraint->id);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"outcome\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, confit_v2_constraint_outcome_name(constraint->outcome));
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"message\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, constraint->message);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, " }%s\n", index + 1U == confit_v2_snapshot_constraint_count(snapshot) ? "" : ",");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  ],\n  \"provenance\": { \"nodes\": [\n");
  for (index = 0U; status == CONFIT_OK && index < confit_v2_snapshot_provenance_node_count(snapshot); ++index) {
    const ConfitV2ProvenanceNode *node = confit_v2_snapshot_provenance_node_at(snapshot, index);
    status = confit_v2_builder_appendf(&builder, "    { \"index\": %llu, \"kind\": ", (unsigned long long)index);
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, confit_v2_provenance_kind_name(node->kind));
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"subject\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, node->subject_id);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, " }%s\n", index + 1U == confit_v2_snapshot_provenance_node_count(snapshot) ? "" : ",");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  ], \"edges\": [");
  for (index = 0U; status == CONFIT_OK && index < confit_v2_snapshot_provenance_edge_count(snapshot); ++index) {
    const ConfitV2ProvenanceEdge *edge = confit_v2_snapshot_provenance_edge_at(snapshot, index);
    status = confit_v2_builder_appendf(&builder, "%s{ \"from\": %llu, \"to\": %llu }", index == 0U ? "" : ", ", (unsigned long long)edge->from_index, (unsigned long long)edge->to_index);
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "] }\n}\n");
  if (status == CONFIT_OK) { *out = confit_v2_builder_take(&builder); if (*out == 0) status = CONFIT_ERR_INTERNAL; }
  confit_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_v2_generate_explain(const ConfitV2Snapshot *snapshot,
                                                char **out) {
  ConfitV2ArtifactBuilder builder;
  size_t index;
  ConfitStatus status;

  confit_v2_builder_init(&builder);
  status = confit_v2_builder_appendf(&builder, "Identity\nproject: %s\nprofile: %s\ntarget: %s\nsnapshot_hash: 0x%016llX\n\nSelected Inputs\nsource_hash: 0x%016llX\ninput_hash: 0x%016llX\n\nEffective Values\n", confit_v2_snapshot_project_name(snapshot), confit_v2_snapshot_profile_name(snapshot) != 0 ? confit_v2_snapshot_profile_name(snapshot) : "none", confit_v2_snapshot_target_name(snapshot) != 0 ? confit_v2_snapshot_target_name(snapshot) : "none", (unsigned long long)confit_v2_snapshot_semantic_hash(snapshot), (unsigned long long)confit_v2_snapshot_source_hash(snapshot), (unsigned long long)confit_v2_snapshot_input_hash(snapshot));
  for (index = 0U; status == CONFIT_OK && index < confit_v2_snapshot_option_count(snapshot); ++index) {
    const ConfitV2SnapshotOption *option = confit_v2_snapshot_option_at(snapshot, index);
    status = confit_v2_builder_appendf(&builder, "%s: %s; requested=%s; effective=%s; available=%s; visible=%s\n", option->id, confit_v2_option_type_name(option->type), option->requested.is_present ? confit_v2_assignment_origin_name(option->requested.origin) : "absent", confit_v2_effective_origin_name(option->effective_origin), option->available ? "true" : "false", option->visible ? "true" : "false");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\nChoices\n");
  for (index = 0U; status == CONFIT_OK && index < confit_v2_snapshot_choice_count(snapshot); ++index) {
    const ConfitV2SnapshotChoice *choice = confit_v2_snapshot_choice_at(snapshot, index);
    status = confit_v2_builder_appendf(&builder, "%s: selected=%s; available=%s; visible=%s\n", choice->id, choice->selected_member_id != 0 ? choice->selected_member_id : "none", choice->available ? "true" : "false", choice->visible ? "true" : "false");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\nConstraints\n");
  for (index = 0U; status == CONFIT_OK && index < confit_v2_snapshot_constraint_count(snapshot); ++index) {
    const ConfitV2SnapshotConstraint *constraint = confit_v2_snapshot_constraint_at(snapshot, index);
    status = confit_v2_builder_appendf(&builder, "%s: %s\n", constraint->id, confit_v2_constraint_outcome_name(constraint->outcome));
  }
  if (status == CONFIT_OK) { *out = confit_v2_builder_take(&builder); if (*out == 0) status = CONFIT_ERR_INTERNAL; }
  confit_v2_builder_clear(&builder);
  return status;
}

static int confit_v2_input_compare(const void *left, const void *right) {
  const ConfitV2ArtifactInput *const *left_input = left;
  const ConfitV2ArtifactInput *const *right_input = right;
  return strcmp((*left_input)->path, (*right_input)->path);
}

static ConfitStatus confit_v2_generate_inputs(const ConfitV2Snapshot *snapshot,
                                               const ConfitV2ArtifactOptions *options,
                                               char **out) {
  ConfitV2ArtifactBuilder builder;
  const ConfitV2ArtifactInput **ordered = 0;
  size_t index;
  ConfitStatus status;

  if (options->input_count > 0U && options->inputs == 0) return CONFIT_ERR_INVALID_ARGUMENT;
  if (options->input_count > 0U) {
    ordered = calloc(options->input_count, sizeof(*ordered));
    if (ordered == 0) return CONFIT_ERR_INTERNAL;
    for (index = 0U; index < options->input_count; ++index) ordered[index] = &options->inputs[index];
    qsort(ordered, options->input_count, sizeof(*ordered), confit_v2_input_compare);
  }
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n");
  if (status == CONFIT_OK) status = confit_v2_append_identity_json(&builder, snapshot, "confit-inputs-v2", 1);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  \"inputs\": [\n");
  for (index = 0U; status == CONFIT_OK && index < options->input_count; ++index) {
    const ConfitV2ArtifactInput *input = ordered[index];
    if (input->path == 0 || input->content_hash == 0 || input->role == 0) { status = CONFIT_ERR_INVALID_ARGUMENT; break; }
    status = confit_v2_builder_append(&builder, "    { \"path\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, input->path);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"content_hash\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, input->content_hash);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"role\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, input->role);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, " }%s\n", index + 1U == options->input_count ? "" : ",");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  ]\n}\n");
  if (status == CONFIT_OK) { *out = confit_v2_builder_take(&builder); if (*out == 0) status = CONFIT_ERR_INTERNAL; }
  confit_v2_builder_clear(&builder);
  free(ordered);
  return status;
}

static uint64_t confit_v2_value_hash(uint64_t hash, const ConfitV2Value *value) {
  const char *text;
  size_t index;
  char canonical[64];

  hash ^= (uint64_t)value->kind;
  hash *= UINT64_C(1099511628211);
  if (value->kind == CONFIT_V2_VALUE_STRING_LIST) {
    for (index = 0U; index < value->as.string_list.count; ++index) {
      const ConfitV2Value element = {
          CONFIT_V2_VALUE_STRING,
          {.string_value = value->as.string_list.items[index]}};
      hash = confit_v2_value_hash(hash, &element);
    }
    return hash;
  }
  switch (value->kind) {
  case CONFIT_V2_VALUE_BOOL:
    (void)snprintf(canonical, sizeof(canonical), "%d", value->as.bool_value);
    text = canonical;
    break;
  case CONFIT_V2_VALUE_TRISTATE:
    canonical[0] = value->as.tristate_value;
    canonical[1] = '\0';
    text = canonical;
    break;
  case CONFIT_V2_VALUE_INT:
    (void)snprintf(canonical, sizeof(canonical), "%lld",
                   (long long)value->as.int_value);
    text = canonical;
    break;
  case CONFIT_V2_VALUE_UINT:
    (void)snprintf(canonical, sizeof(canonical), "%llu",
                   (unsigned long long)value->as.uint_value);
    text = canonical;
    break;
  case CONFIT_V2_VALUE_FLOAT:
    (void)snprintf(canonical, sizeof(canonical), "%a", value->as.float_value);
    text = canonical;
    break;
  case CONFIT_V2_VALUE_STRING:
    text = value->as.string_value != 0 ? value->as.string_value : "";
    break;
  case CONFIT_V2_VALUE_UNSET:
  default:
    text = "";
    break;
  }
  for (index = 0U; text[index] != '\0'; ++index) {
    hash ^= (uint64_t)(unsigned char)text[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static ConfitStatus confit_v2_generate_changes(const ConfitV2Snapshot *snapshot,
                                                char **out) {
  ConfitV2ArtifactBuilder builder;
  size_t index;
  ConfitStatus status;
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n");
  if (status == CONFIT_OK) status = confit_v2_append_identity_json(&builder, snapshot, "confit-changes-v2", 1);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  \"options\": [\n");
  for (index = 0U; status == CONFIT_OK && index < confit_v2_snapshot_option_count(snapshot); ++index) {
    const ConfitV2SnapshotOption *option = confit_v2_snapshot_option_at(snapshot, index);
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = confit_v2_value_hash(hash, &option->effective_value);
    status = confit_v2_builder_append(&builder, "    { \"id\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, option->id);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, ", \"effective_hash\": \"0x%016llX\", \"build_hash\": \"0x%016llX\" }%s\n", (unsigned long long)hash, (unsigned long long)(hash ^ (uint64_t)option->emit_mask), index + 1U == confit_v2_snapshot_option_count(snapshot) ? "" : ",");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  ]\n}\n");
  if (status == CONFIT_OK) { *out = confit_v2_builder_take(&builder); if (*out == 0) status = CONFIT_ERR_INTERNAL; }
  confit_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_v2_generate_cmake(const ConfitV2Snapshot *snapshot,
                                              char **out) {
  ConfitV2ArtifactBuilder builder;
  size_t index;
  ConfitStatus status;
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "set(CONFIT_SCHEMA_VERSION \"2\")\nset(CONFIT_RESOLVER_ABI \"confit-resolver-v2\")\nset(CONFIT_ARTIFACT_ABI \"confit-artifact-v2\")\n");
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, "set(CONFIT_SOURCE_HASH \"0x%016llX\")\nset(CONFIT_INPUT_HASH \"0x%016llX\")\nset(CONFIT_SNAPSHOT_HASH \"0x%016llX\")\n", (unsigned long long)confit_v2_snapshot_source_hash(snapshot), (unsigned long long)confit_v2_snapshot_input_hash(snapshot), (unsigned long long)confit_v2_snapshot_semantic_hash(snapshot));
  for (index = 0U; status == CONFIT_OK && index < confit_v2_snapshot_option_count(snapshot); ++index) {
    const ConfitV2SnapshotOption *option = confit_v2_snapshot_option_at(snapshot, index);
    char *macro = 0;
    if ((option->emit_mask & CONFIT_V2_EMIT_CMAKE) == 0U) continue;
    status = confit_v2_make_option_macro(snapshot, option->id, &macro);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, "set(%s_SET \"%s\")\n", macro, option->effective_is_set ? "ON" : "OFF");
    if (status == CONFIT_OK && option->effective_is_set) {
      status = confit_v2_builder_appendf(&builder, "set(%s ", macro);
      if (status == CONFIT_OK) status = confit_v2_append_cmake_value(&builder, &option->effective_value, option->type);
      if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ")\n");
    }
    free(macro);
  }
  if (status == CONFIT_OK) { *out = confit_v2_builder_take(&builder); if (*out == 0) status = CONFIT_ERR_INTERNAL; }
  confit_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_v2_append_qsm_value(ConfitV2ArtifactBuilder *builder,
                                                const ConfitV2Snapshot *snapshot,
                                                const ConfitV2SnapshotOption *option) {
  ConfitStatus status = confit_v2_builder_append(builder, "{ type = ");
  if (status == CONFIT_OK) status = confit_v2_append_lua_string(builder, confit_v2_option_type_name(option->type));
  if (status == CONFIT_OK) status = confit_v2_builder_append(builder, ", requested = { state = ");
  if (status == CONFIT_OK) status = confit_v2_append_lua_string(builder, !option->requested.is_present ? "absent" : option->requested.is_unset ? "unset" : "set");
  if (status == CONFIT_OK && option->requested.is_set) status = confit_v2_builder_append(builder, ", value = ");
  if (status == CONFIT_OK && option->requested.is_set) status = confit_v2_append_lua_value(builder, &option->requested.value, option->type);
  if (status == CONFIT_OK && option->requested.is_present) status = confit_v2_builder_append(builder, ", source = ");
  if (status == CONFIT_OK && option->requested.is_present) status = confit_v2_append_lua_string(builder, confit_v2_source_label(snapshot, option->requested.source_path));
  if (status == CONFIT_OK) status = confit_v2_builder_append(builder, " }, effective = { state = ");
  if (status == CONFIT_OK) status = confit_v2_append_lua_string(builder, option->effective_is_set ? "set" : "unset");
  if (status == CONFIT_OK && option->effective_is_set) status = confit_v2_builder_append(builder, ", value = ");
  if (status == CONFIT_OK && option->effective_is_set) status = confit_v2_append_lua_value(builder, &option->effective_value, option->type);
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(builder, " }, available = %s, visible = %s }", option->available ? "true" : "false", option->visible ? "true" : "false");
  return status;
}

static ConfitStatus confit_v2_generate_qsm(const ConfitV2Snapshot *snapshot,
                                            const char *schema, unsigned mask,
                                            char **out) {
  ConfitV2ArtifactBuilder builder;
  size_t index;
  size_t emitted = 0U;
  ConfitStatus status;
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "return {\n  schema = ");
  if (status == CONFIT_OK) status = confit_v2_append_lua_string(&builder, schema);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ",\n  schema_version = 2,\n  resolver_abi = \"confit-resolver-v2\",\n  artifact_abi = \"confit-artifact-v2\",\n  project = ");
  if (status == CONFIT_OK) status = confit_v2_append_lua_string(&builder, confit_v2_snapshot_project_name(snapshot));
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ",\n  profile = ");
  if (status == CONFIT_OK) status = confit_v2_append_lua_string(&builder, confit_v2_snapshot_profile_name(snapshot));
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ",\n  target = ");
  if (status == CONFIT_OK) status = confit_v2_append_lua_string(&builder, confit_v2_snapshot_target_name(snapshot));
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, ",\n  source_hash = \"0x%016llX\",\n  snapshot_hash = \"0x%016llX\",\n  values = {\n", (unsigned long long)confit_v2_snapshot_source_hash(snapshot), (unsigned long long)confit_v2_snapshot_semantic_hash(snapshot));
  for (index = 0U; status == CONFIT_OK && index < confit_v2_snapshot_option_count(snapshot); ++index) {
    const ConfitV2SnapshotOption *option = confit_v2_snapshot_option_at(snapshot, index);
    if (mask != 0U && (option->emit_mask & mask) == 0U) continue;
    if (emitted != 0U) status = confit_v2_builder_append(&builder, ",\n");
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "    [");
    if (status == CONFIT_OK) status = confit_v2_append_lua_string(&builder, option->id);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "] = ");
    if (status == CONFIT_OK) status = confit_v2_append_qsm_value(&builder, snapshot, option);
    emitted += 1U;
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n  },\n}\n");
  if (status == CONFIT_OK) { *out = confit_v2_builder_take(&builder); if (*out == 0) status = CONFIT_ERR_INTERNAL; }
  confit_v2_builder_clear(&builder);
  return status;
}

static const char *confit_v2_graph_name(ConfitV2CompiledGraphKind kind) {
  switch (kind) {
  case CONFIT_V2_COMPILED_GRAPH_EVALUATION: return "evaluation";
  case CONFIT_V2_COMPILED_GRAPH_VISIBILITY: return "visibility";
  case CONFIT_V2_COMPILED_GRAPH_CHOICE: return "choice";
  case CONFIT_V2_COMPILED_GRAPH_CONSTRAINT: return "constraint_reference";
  default: return "unknown";
  }
}

static ConfitStatus confit_v2_generate_graph(const ConfitV2Snapshot *snapshot,
                                              const ConfitV2ArtifactOptions *options,
                                              char **out, ConfitDiagnostic *diagnostic) {
  static const ConfitV2CompiledGraphKind kinds[] = { CONFIT_V2_COMPILED_GRAPH_EVALUATION, CONFIT_V2_COMPILED_GRAPH_VISIBILITY, CONFIT_V2_COMPILED_GRAPH_CHOICE, CONFIT_V2_COMPILED_GRAPH_CONSTRAINT };
  ConfitV2ArtifactBuilder builder;
  size_t kind_index;
  size_t index;
  ConfitStatus status;
  (void)diagnostic;
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n");
  if (status == CONFIT_OK) status = confit_v2_append_identity_json(&builder, snapshot, "confit-graph-v2", 1);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  \"graphs\": {\n");
  for (kind_index = 0U; status == CONFIT_OK && kind_index < sizeof(kinds) / sizeof(kinds[0]); ++kind_index) {
    const ConfitV2CompiledGraph *graph = options->compiled != 0 ? confit_v2_compiled_structure_graph(options->compiled, kinds[kind_index]) : 0;
    status = confit_v2_builder_appendf(&builder, "    \"%s\": [", confit_v2_graph_name(kinds[kind_index]));
    for (index = 0U; status == CONFIT_OK && graph != 0 && index < graph->edge_count; ++index) {
      if (index != 0U) status = confit_v2_builder_append(&builder, ", ");
      if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "{ \"owner\": ");
      if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, graph->edges[index].owner_id);
      if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"target\": ");
      if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, graph->edges[index].target->id);
      if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, " }");
    }
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, "]%s\n", kind_index + 1U == sizeof(kinds) / sizeof(kinds[0]) ? "" : ",");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  },\n  \"provenance\": { \"nodes\": [");
  for (index = 0U; status == CONFIT_OK && index < confit_v2_snapshot_provenance_node_count(snapshot); ++index) {
    const ConfitV2ProvenanceNode *node = confit_v2_snapshot_provenance_node_at(snapshot, index);
    if (index != 0U) status = confit_v2_builder_append(&builder, ", ");
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "{ \"kind\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, confit_v2_provenance_kind_name(node->kind));
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"subject\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, node->subject_id);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, " }");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "] },\n  \"reverse_invalidation\": [\n");
  for (index = 0U; status == CONFIT_OK && index < confit_v2_snapshot_option_count(snapshot); ++index) {
    const ConfitV2SnapshotOption *option = confit_v2_snapshot_option_at(snapshot, index);
    ConfitV2InvalidationSet *set = 0;
    size_t affected_index;
    status = confit_v2_snapshot_invalidate(snapshot, option->id, &set, diagnostic);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "    { \"option\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, option->id);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"affected\": [");
    for (affected_index = 0U; status == CONFIT_OK && affected_index < confit_v2_invalidation_set_count(set); ++affected_index) {
      const ConfitV2InvalidationNode *node = confit_v2_invalidation_set_at(set, affected_index);
      if (affected_index != 0U) status = confit_v2_builder_append(&builder, ", ");
      if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "{ \"kind\": ");
      if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, confit_v2_invalidation_kind_name(node->kind));
      if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"id\": ");
      if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, node->id);
      if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, " }");
    }
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, "] }%s\n", index + 1U == confit_v2_snapshot_option_count(snapshot) ? "" : ",");
    confit_v2_invalidation_set_free(set);
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  ]\n}\n");
  if (status == CONFIT_OK) { *out = confit_v2_builder_take(&builder); if (*out == 0) status = CONFIT_ERR_INTERNAL; }
  confit_v2_builder_clear(&builder);
  return status;
}

ConfitStatus confit_v2_generate_artifacts(
    const ConfitV2Snapshot *snapshot, const ConfitV2ArtifactOptions *options,
    ConfitV2ArtifactSet *out_artifacts, ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactOptions defaults;
  ConfitStatus status;
  const char *selection_name;

  if (snapshot == 0 || out_artifacts == 0 ||
      confit_v2_snapshot_project_name(snapshot) == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          kInvalidArgument);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(out_artifacts, 0, sizeof(*out_artifacts));
  memset(&defaults, 0, sizeof(defaults));
  options = options != 0 ? options : &defaults;
  selection_name = options->selection_name != 0 ? options->selection_name
                                                  : "build_selection";
  if (!confit_v2_is_artifact_name(selection_name)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, selection_name,
                          0, 0, kInvalidArgument);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  {
    const size_t selection_name_size = strlen(selection_name);
    out_artifacts->selection_name = (char *)malloc(selection_name_size + 1U);
    if (out_artifacts->selection_name != 0) {
      memcpy(out_artifacts->selection_name, selection_name,
             selection_name_size + 1U);
    }
  }
  if (out_artifacts->selection_name == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  status = confit_v2_generate_header(snapshot, &out_artifacts->config_header, diagnostic);
  if (status == CONFIT_OK) status = confit_v2_generate_report(snapshot, &out_artifacts->report_json);
  if (status == CONFIT_OK) status = confit_v2_generate_explain(snapshot, &out_artifacts->explain_text);
  if (status == CONFIT_OK) status = confit_v2_generate_graph(snapshot, options, &out_artifacts->graph_json, diagnostic);
  if (status == CONFIT_OK) status = confit_v2_generate_inputs(snapshot, options, &out_artifacts->inputs_json);
  if (status == CONFIT_OK) status = confit_v2_generate_changes(snapshot, &out_artifacts->changes_json);
  if (status == CONFIT_OK) status = confit_v2_generate_cmake(snapshot, &out_artifacts->cmake_fragment);
  if (status == CONFIT_OK) status = confit_v2_generate_qsm(snapshot, "confit-config-manifest-v2", 0U, &out_artifacts->qsm_module);
  if (status == CONFIT_OK) status = confit_v2_generate_qsm(snapshot, "confit-build-selection-v2", CONFIT_V2_EMIT_SELECTION, &out_artifacts->selection_module);
  if (status != CONFIT_OK) {
    confit_v2_artifact_set_clear(out_artifacts);
    if (diagnostic != 0 && diagnostic->message == 0) {
      confit_diagnostic_set(diagnostic, status, 0, 0, 0, kAllocationFailed);
    }
  }
  return status;
}

void confit_v2_artifact_set_clear(ConfitV2ArtifactSet *artifacts) {
  if (artifacts == 0) return;
  free(artifacts->config_header);
  free(artifacts->report_json);
  free(artifacts->explain_text);
  free(artifacts->graph_json);
  free(artifacts->inputs_json);
  free(artifacts->changes_json);
  free(artifacts->cmake_fragment);
  free(artifacts->qsm_module);
  free(artifacts->selection_module);
  free(artifacts->selection_name);
  memset(artifacts, 0, sizeof(*artifacts));
}
