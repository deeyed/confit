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
#else
#include <unistd.h>
#endif

#include "confit/host.h"
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

/* ABI v3 deliberately uses a self-contained SHA-256 implementation.  A host
 * OpenSSL/CommonCrypto dependency would make configuration identity depend on
 * an ambient library or its provider policy. */
typedef struct ConfitV3Sha256 {
  uint32_t state[8];
  uint64_t bit_count;
  unsigned char block[64];
  size_t block_size;
} ConfitV3Sha256;

static uint32_t confit_v3_rotr(uint32_t value, unsigned int shift) {
  return (value >> shift) | (value << (32U - shift));
}

static void confit_v3_sha256_init(ConfitV3Sha256 *hash) {
  static const uint32_t initial[] = {
      UINT32_C(0x6A09E667), UINT32_C(0xBB67AE85), UINT32_C(0x3C6EF372),
      UINT32_C(0xA54FF53A), UINT32_C(0x510E527F), UINT32_C(0x9B05688C),
      UINT32_C(0x1F83D9AB), UINT32_C(0x5BE0CD19)};

  memcpy(hash->state, initial, sizeof(initial));
  hash->bit_count = 0U;
  hash->block_size = 0U;
}

static void confit_v3_sha256_compress(ConfitV3Sha256 *hash,
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
    const uint32_t small0 = confit_v3_rotr(schedule[index - 15U], 7U) ^
                            confit_v3_rotr(schedule[index - 15U], 18U) ^
                            (schedule[index - 15U] >> 3U);
    const uint32_t small1 = confit_v3_rotr(schedule[index - 2U], 17U) ^
                            confit_v3_rotr(schedule[index - 2U], 19U) ^
                            (schedule[index - 2U] >> 10U);
    schedule[index] = schedule[index - 16U] + small0 + schedule[index - 7U] +
                      small1;
  }
  a = hash->state[0]; b = hash->state[1]; c = hash->state[2]; d = hash->state[3];
  e = hash->state[4]; f = hash->state[5]; g = hash->state[6]; h = hash->state[7];
  for (index = 0U; index < 64U; ++index) {
    const uint32_t big1 = confit_v3_rotr(e, 6U) ^ confit_v3_rotr(e, 11U) ^
                          confit_v3_rotr(e, 25U);
    const uint32_t choose = (e & f) ^ ((~e) & g);
    const uint32_t temporary1 = h + big1 + choose + constants[index] + schedule[index];
    const uint32_t big0 = confit_v3_rotr(a, 2U) ^ confit_v3_rotr(a, 13U) ^
                          confit_v3_rotr(a, 22U);
    const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const uint32_t temporary2 = big0 + majority;
    h = g; g = f; f = e; e = d + temporary1; d = c; c = b; b = a;
    a = temporary1 + temporary2;
  }
  hash->state[0] += a; hash->state[1] += b; hash->state[2] += c;
  hash->state[3] += d; hash->state[4] += e; hash->state[5] += f;
  hash->state[6] += g; hash->state[7] += h;
}

static void confit_v3_sha256_update(ConfitV3Sha256 *hash,
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
      confit_v3_sha256_compress(hash, hash->block);
      hash->block_size = 0U;
    }
  }
}

static void confit_v3_sha256_final(ConfitV3Sha256 *hash,
                                   unsigned char output[32]) {
  size_t index;
  const uint64_t bit_count = hash->bit_count;

  hash->block[hash->block_size++] = 0x80U;
  if (hash->block_size > 56U) {
    while (hash->block_size < 64U) hash->block[hash->block_size++] = 0U;
    confit_v3_sha256_compress(hash, hash->block);
    hash->block_size = 0U;
  }
  while (hash->block_size < 56U) hash->block[hash->block_size++] = 0U;
  for (index = 0U; index < 8U; ++index) {
    hash->block[63U - index] = (unsigned char)(bit_count >> (index * 8U));
  }
  confit_v3_sha256_compress(hash, hash->block);
  for (index = 0U; index < 8U; ++index) {
    output[index * 4U] = (unsigned char)(hash->state[index] >> 24U);
    output[index * 4U + 1U] = (unsigned char)(hash->state[index] >> 16U);
    output[index * 4U + 2U] = (unsigned char)(hash->state[index] >> 8U);
    output[index * 4U + 3U] = (unsigned char)hash->state[index];
  }
}

static void confit_v3_sha256_text(const char *text, char output[65]) {
  static const char digits[] = "0123456789abcdef";
  unsigned char bytes[32];
  ConfitV3Sha256 hash;
  size_t index;

  confit_v3_sha256_init(&hash);
  confit_v3_sha256_update(&hash, (const unsigned char *)text, strlen(text));
  confit_v3_sha256_final(&hash, bytes);
  for (index = 0U; index < sizeof(bytes); ++index) {
    output[index * 2U] = digits[bytes[index] >> 4U];
    output[index * 2U + 1U] = digits[bytes[index] & 0x0FU];
  }
  output[64] = '\0';
}

void confit_v3_sha256_hex(const char *text, char output[65]) {
  if (output == 0) return;
  if (text == 0) {
    output[0] = '\0';
    return;
  }
  confit_v3_sha256_text(text, output);
}

static int confit_v3_is_sha256(const char *text) {
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

static int confit_v3_is_safe_atom(const char *text, int path) {
  size_t index;
  size_t segment_start = 0U;

  if (text == 0 || text[0] == '\0' || (path && text[0] == '/')) return 0;
  for (index = 0U; text[index] != '\0'; ++index) {
    const char value = text[index];
    const int allowed = (value >= 'a' && value <= 'z') ||
                        (value >= 'A' && value <= 'Z') ||
                        (value >= '0' && value <= '9') || value == '_' ||
                        value == '-' || value == '.' || value == '+' ||
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

static ConfitStatus confit_v3_append_identity(
    ConfitV2ArtifactBuilder *builder, const ConfitV2Snapshot *snapshot,
    const char *schema, const char *tool_identity) {
  ConfitStatus status = confit_v2_builder_append(builder, "  \"schema\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(builder, schema);
  if (status == CONFIT_OK) status = confit_v2_builder_append(
      builder, ",\n  \"artifact_abi\": 3,\n  \"resolver_abi\": \"confit-resolver-v2\",\n  \"tool\": ");
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

static ConfitStatus confit_v3_append_component_id_array(
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

static ConfitStatus confit_v3_append_component_root_array(
    ConfitV2ArtifactBuilder *builder, const ConfitComponentClosure *closure) {
  ConfitStatus status = confit_v2_builder_append(builder, "[");
  size_t index;
  for (index = 0U; status == CONFIT_OK && closure != 0 && index < closure->root_count;
       ++index) {
    if (index != 0U) status = confit_v2_builder_append(builder, ", ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(
        builder, closure->root_ids[index]);
  }
  return status == CONFIT_OK ? confit_v2_builder_append(builder, "]") : status;
}

static ConfitStatus confit_v3_generate_selection(
    const ConfitV2Snapshot *snapshot, const char *tool_identity,
    const ConfitComponentCatalog *catalog, const ConfitComponentClosure *closure,
    char **out) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  size_t index;

  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n");
  if (status == CONFIT_OK) status = confit_v3_append_identity(
      &builder, snapshot, "confit-selection-v3", tool_identity);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ",\n  \"options\": [\n");
  for (index = 0U; status == CONFIT_OK &&
                    index < confit_v2_snapshot_option_count(snapshot); ++index) {
    const ConfitV2SnapshotOption *option = confit_v2_snapshot_option_at(snapshot, index);
    status = confit_v2_builder_append(&builder, "    { \"id\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, option->id);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"type\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, confit_v2_option_type_name(option->type));
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
      &builder, "  ],\n  \"components\": { \"catalog_state\": ");
  if (status == CONFIT_OK) status = confit_v2_append_json_string(
      &builder, catalog != 0 ? "available" : "unavailable");
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"roots\": ");
  if (status == CONFIT_OK) status = confit_v3_append_component_root_array(&builder, closure);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"selected\": ");
  if (status == CONFIT_OK) status = confit_v3_append_component_id_array(&builder, closure);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, " }\n}\n");
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_v3_generate_report(
    const ConfitV2Snapshot *snapshot, const char *tool_identity,
    const ConfitComponentCatalog *catalog, const ConfitComponentClosure *closure,
    char **out) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;

  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n");
  if (status == CONFIT_OK) status = confit_v3_append_identity(
      &builder, snapshot, "confit-report-v3", tool_identity);
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

static ConfitStatus confit_v3_generate_inputs(
    const ConfitV2Snapshot *snapshot, const ConfitV3ArtifactOptions *options,
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
    qsort(ordered, options->input_count, sizeof(*ordered), confit_v2_input_compare);
  }
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n");
  if (status == CONFIT_OK) status = confit_v3_append_identity(
      &builder, snapshot, "confit-inputs-v3", tool_identity);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ",\n  \"inputs\": [\n");
  for (index = 0U; status == CONFIT_OK && index < options->input_count; ++index) {
    const ConfitV2ArtifactInput *input = ordered[index];
    if (input->path == 0 || input->role == 0 || !confit_v3_is_sha256(input->content_hash) ||
        !confit_v3_is_safe_atom(input->path, 1) || !confit_v3_is_safe_atom(input->role, 0) ||
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

static ConfitStatus confit_v3_generate_header(const ConfitV2Snapshot *snapshot,
                                                char **out,
                                                ConfitDiagnostic *diagnostic) {
  char *legacy = 0;
  const char *needle = "#define CONFIT_ARTIFACT_ABI \"confit-artifact-v2\"";
  char *position;
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status = confit_v2_generate_header(snapshot, &legacy, diagnostic);

  if (status != CONFIT_OK) return status;
  position = strstr(legacy, needle);
  if (position == 0) {
    free(legacy);
    return CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append_n(&builder, legacy,
                                      (size_t)(position - legacy));
  if (status == CONFIT_OK) status = confit_v2_builder_append(
      &builder, "#define CONFIT_ARTIFACT_ABI 3");
  if (status == CONFIT_OK) status = confit_v2_builder_append(
      &builder, position + strlen(needle));
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_clear(&builder);
  free(legacy);
  return status;
}

static ConfitStatus confit_v3_append_make_value(
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
    if (!confit_v3_is_safe_atom(value->as.string_value,
                                option->type == CONFIT_V2_OPTION_TYPE_PATH)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0U, 0U,
                            "unsafe value cannot enter generated Make syntax");
      return CONFIT_ERR_SCHEMA;
    }
    return confit_v2_builder_append(builder, value->as.string_value);
  case CONFIT_V2_VALUE_STRING_LIST:
    for (index = 0U; index < value->as.string_list.count; ++index) {
      if (!confit_v3_is_safe_atom(value->as.string_list.items[index],
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
static int confit_v3_make_value_is_safe(const ConfitV2SnapshotOption *option) {
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
    return confit_v3_is_safe_atom(
        value->as.string_value, option->type == CONFIT_V2_OPTION_TYPE_PATH);
  case CONFIT_V2_VALUE_STRING_LIST:
    for (index = 0U; index < value->as.string_list.count; ++index) {
      if (!confit_v3_is_safe_atom(
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

static ConfitStatus confit_v3_generate_make_values(
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
    if (!confit_v3_make_value_is_safe(option)) continue;
    status = confit_v2_make_option_macro(snapshot, option->id, &macro);
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(&builder, "%s:= ", macro);
    if (status == CONFIT_OK) status = confit_v3_append_make_value(&builder, option, diagnostic);
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

static ConfitStatus confit_v3_component_make_identifier(
    const char *id, char *out, size_t out_size) {
  size_t index;
  if (id == 0 || strlen(id) + 1U > out_size) return CONFIT_ERR_INVALID_ARGUMENT;
  for (index = 0U; id[index] != '\0'; ++index) {
    out[index] = id[index] == '.' ? '_' : id[index];
  }
  out[index] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_v3_generate_components_mk(
    const ConfitComponentCatalog *catalog, const ConfitComponentClosure *closure,
    char **out, ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  size_t index;
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder,
      "# Generated by Confit; selected component closure only.\nPARUS_COMPONENT_IDS:=");
  for (index = 0U; status == CONFIT_OK && closure != 0 && index < closure->component_count;
       ++index) {
    if (!confit_v3_is_safe_atom(closure->ordered[index]->id, 0)) {
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
    status = confit_v3_component_make_identifier(component->id, identifier,
                                                  sizeof(identifier));
    if (status == CONFIT_OK &&
        (!confit_v3_is_safe_atom(component->manifest_path, 1) ||
         !confit_v3_is_safe_atom(component->makefile_path, 1))) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, component->id, 0U, 0U,
                            "unsafe component path cannot enter generated Make syntax");
      status = CONFIT_ERR_SCHEMA;
    }
    if (status == CONFIT_OK) status = confit_v2_builder_appendf(
        &builder, "\nPARUS_COMPONENT_%s_MANIFEST:= %s\nPARUS_COMPONENT_%s_MAKEFILE:= %s",
        identifier, component->manifest_path, identifier, component->makefile_path);
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

static ConfitStatus confit_v3_append_component_atom_array(
    ConfitV2ArtifactBuilder *builder, char *const *items, size_t count) {
  ConfitStatus status = confit_v2_builder_append(builder, "[");
  size_t index;
  for (index = 0U; status == CONFIT_OK && index < count; ++index) {
    if (index != 0U) status = confit_v2_builder_append(builder, ", ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(builder, items[index]);
  }
  return status == CONFIT_OK ? confit_v2_builder_append(builder, "]") : status;
}

static ConfitStatus confit_v3_generate_component_catalog_json(
    const ConfitComponentCatalog *catalog, const ConfitComponentClosure *closure,
    char **out) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  size_t index;
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n  \"schema\": \"confit-component-catalog-v1\",\n  \"state\": ");
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
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"manifest\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, component->manifest_path);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"makefile\": ");
    if (status == CONFIT_OK) status = confit_v2_append_json_string(&builder, component->makefile_path);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"dependencies\": ");
    if (status == CONFIT_OK) status = confit_v3_append_component_atom_array(
        &builder, component->component_dependencies, component->component_dependency_count);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"kapi_requires\": ");
    if (status == CONFIT_OK) status = confit_v3_append_component_atom_array(
        &builder, component->kapi_requires, component->kapi_requirement_count);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"kapi_provides\": ");
    if (status == CONFIT_OK) status = confit_v3_append_component_atom_array(
        &builder, component->kapi_provides, component->kapi_provide_count);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ", \"capabilities\": ");
    if (status == CONFIT_OK) status = confit_v3_append_component_atom_array(
        &builder, component->capabilities, component->capability_count);
    if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, " } ");
    if (status == CONFIT_OK) status = confit_v2_builder_append(
        &builder, index + 1U == catalog->component_count ? "\n" : ",\n");
  }
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "  ],\n  \"roots\": ");
  if (status == CONFIT_OK) status = confit_v3_append_component_root_array(&builder, closure);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, ",\n  \"selected\": ");
  if (status == CONFIT_OK) status = confit_v3_append_component_id_array(&builder, closure);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\n}\n");
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_clear(&builder);
  return status;
}

typedef struct ConfitV3NamedText {
  const char *path;
  const char *text;
  char sha256[65];
} ConfitV3NamedText;

static void confit_v3_digest_named_texts(ConfitV3NamedText *texts,
                                         size_t text_count,
                                         char output[65]) {
  ConfitV3Sha256 hash;
  size_t index;

  confit_v3_sha256_init(&hash);
  confit_v3_sha256_update(&hash,
                          (const unsigned char *)"confit.bundle.v3.identity\n",
                          strlen("confit.bundle.v3.identity\n"));
  for (index = 0U; index < text_count; ++index) {
    const char separator = '\n';
    confit_v3_sha256_text(texts[index].text, texts[index].sha256);
    confit_v3_sha256_update(&hash, (const unsigned char *)texts[index].path,
                            strlen(texts[index].path));
    confit_v3_sha256_update(&hash, (const unsigned char *)&separator, 1U);
    confit_v3_sha256_update(&hash,
                            (const unsigned char *)texts[index].sha256,
                            strlen(texts[index].sha256));
    confit_v3_sha256_update(&hash, (const unsigned char *)&separator, 1U);
  }
  {
    static const char digits[] = "0123456789abcdef";
    unsigned char bytes[32];
    confit_v3_sha256_final(&hash, bytes);
    for (index = 0U; index < sizeof(bytes); ++index) {
      output[index * 2U] = digits[bytes[index] >> 4U];
      output[index * 2U + 1U] = digits[bytes[index] & 0x0FU];
    }
    output[64] = '\0';
  }
}

static ConfitStatus confit_v3_generate_config_mk(
    const ConfitV2Snapshot *snapshot, const char *bundle_digest, char **out,
    ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  const char *project = confit_v2_snapshot_project_name(snapshot);
  const char *profile = confit_v2_snapshot_profile_name(snapshot);
  const char *target = confit_v2_snapshot_target_name(snapshot);

  if (!confit_v3_is_safe_atom(project, 0) ||
      (profile != 0 && !confit_v3_is_safe_atom(profile, 0)) ||
      (target != 0 && !confit_v3_is_safe_atom(target, 0))) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, 0, 0U, 0U,
                          "unsafe identity cannot enter generated Make syntax");
    return CONFIT_ERR_SCHEMA;
  }
  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder,
      "# Generated by Confit. DO NOT EDIT.\nCONFIT_ARTIFACT_ABI:= 3\nCONFIT_PROJECT:= ");
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, project);
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\nCONFIT_PROFILE:= ");
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, profile != 0 ? profile : "none");
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, "\nCONFIT_TARGET:= ");
  if (status == CONFIT_OK) status = confit_v2_builder_append(&builder, target != 0 ? target : "none");
  if (status == CONFIT_OK) status = confit_v2_builder_appendf(
      &builder, "\nCONFIT_BUNDLE_SHA256:= %s\n.include \"${.PARSEDIR}/config.values.mk\"\n.include \"${.PARSEDIR}/components.mk\"\n",
      bundle_digest);
  if (status == CONFIT_OK) {
    *out = confit_v2_builder_take(&builder);
    if (*out == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_v3_generate_bundle_manifest(
    const ConfitV2Snapshot *snapshot, const char *tool_identity,
    const ConfitV3NamedText *texts, size_t text_count, const char *bundle_digest,
    char **out) {
  ConfitV2ArtifactBuilder builder;
  ConfitStatus status;
  size_t index;

  confit_v2_builder_init(&builder);
  status = confit_v2_builder_append(&builder, "{\n");
  if (status == CONFIT_OK) status = confit_v3_append_identity(
      &builder, snapshot, "confit-bundle-v3", tool_identity);
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

ConfitStatus confit_v3_generate_artifacts(
    const ConfitV2Snapshot *snapshot, const ConfitV3ArtifactOptions *options,
    ConfitV3ArtifactSet *out_artifacts, ConfitDiagnostic *diagnostic) {
  ConfitV3ArtifactOptions defaults;
  ConfitV3NamedText identity_texts[7];
  const char *tool_identity;
  unsigned int mask;
  ConfitStatus status = CONFIT_OK;

  if (snapshot == 0 || out_artifacts == 0 ||
      confit_v2_snapshot_project_name(snapshot) == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          "invalid sealed artifact v3 argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(out_artifacts, 0, sizeof(*out_artifacts));
  memset(&defaults, 0, sizeof(defaults));
  options = options != 0 ? options : &defaults;
  mask = options->artifact_mask == 0U ? CONFIT_V3_ARTIFACT_COMPLETE
                                      : options->artifact_mask;
  if ((mask & ~CONFIT_V3_ARTIFACT_COMPLETE) != 0U) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          "unknown sealed artifact v3 format mask");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  tool_identity = options->tool_identity != 0 ? options->tool_identity
                                              : CONFIT_VERSION_RELEASE;
  if (!confit_v3_is_safe_atom(tool_identity, 0)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, tool_identity,
                          0U, 0U, "unsafe tool identity");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  if ((mask & CONFIT_V3_ARTIFACT_HEADER) != 0U) {
    status = confit_v3_generate_header(snapshot, &out_artifacts->config_header,
                                       diagnostic);
  }
  if (status == CONFIT_OK && (mask & CONFIT_V3_ARTIFACT_SELECTION) != 0U) {
    status = confit_v3_generate_selection(snapshot, tool_identity,
                                          options->component_catalog,
                                          options->component_closure,
                                          &out_artifacts->selection_json);
  }
  if (status == CONFIT_OK && (mask & CONFIT_V3_ARTIFACT_REPORTS) != 0U) {
    status = confit_v3_generate_report(snapshot, tool_identity,
                                       options->component_catalog,
                                       options->component_closure,
                                       &out_artifacts->report_json);
    if (status == CONFIT_OK) status = confit_v3_generate_inputs(
        snapshot, options, tool_identity, &out_artifacts->inputs_json, diagnostic);
  }
  if (status == CONFIT_OK && (mask & CONFIT_V3_ARTIFACT_MAKE_ADAPTER) != 0U) {
    status = confit_v3_generate_make_values(snapshot,
                                            &out_artifacts->config_values_mk,
                                            diagnostic);
    if (status == CONFIT_OK) status = confit_v3_generate_components_mk(
        options->component_catalog, options->component_closure,
        &out_artifacts->components_mk, diagnostic);
    if (status == CONFIT_OK) status = confit_v3_generate_component_catalog_json(
        options->component_catalog, options->component_closure,
        &out_artifacts->component_catalog_json);
  }
  if (status == CONFIT_OK && mask == CONFIT_V3_ARTIFACT_COMPLETE) {
    identity_texts[0] = (ConfitV3NamedText){"config.h", out_artifacts->config_header, {0}};
    identity_texts[1] = (ConfitV3NamedText){"config.selection.json", out_artifacts->selection_json, {0}};
    identity_texts[2] = (ConfitV3NamedText){"config.report.json", out_artifacts->report_json, {0}};
    identity_texts[3] = (ConfitV3NamedText){"config.inputs.json", out_artifacts->inputs_json, {0}};
    identity_texts[4] = (ConfitV3NamedText){"config.values.mk", out_artifacts->config_values_mk, {0}};
    identity_texts[5] = (ConfitV3NamedText){"components.mk", out_artifacts->components_mk, {0}};
    identity_texts[6] = (ConfitV3NamedText){"component.catalog.json", out_artifacts->component_catalog_json, {0}};
    confit_v3_digest_named_texts(identity_texts,
                                 sizeof(identity_texts) / sizeof(identity_texts[0]),
                                 out_artifacts->bundle_digest);
    status = confit_v3_generate_config_mk(snapshot, out_artifacts->bundle_digest,
                                          &out_artifacts->config_mk, diagnostic);
    if (status == CONFIT_OK) {
      ConfitV3NamedText manifest_texts[8];
      memcpy(manifest_texts, identity_texts, sizeof(identity_texts));
      manifest_texts[7] = (ConfitV3NamedText){"config.mk", out_artifacts->config_mk, {0}};
      for (size_t index = 0U; index < sizeof(manifest_texts) / sizeof(manifest_texts[0]); ++index) {
        confit_v3_sha256_text(manifest_texts[index].text, manifest_texts[index].sha256);
      }
      status = confit_v3_generate_bundle_manifest(
          snapshot, tool_identity, manifest_texts,
          sizeof(manifest_texts) / sizeof(manifest_texts[0]),
          out_artifacts->bundle_digest, &out_artifacts->bundle_json);
    }
  }
  if (status != CONFIT_OK) {
    confit_v3_artifact_set_clear(out_artifacts);
    if (diagnostic != 0 && diagnostic->message == 0) {
      confit_diagnostic_set(diagnostic, status, 0, 0U, 0U,
                            "failed to generate sealed artifact v3 bundle");
    }
  }
  return status;
}

void confit_v3_artifact_set_clear(ConfitV3ArtifactSet *artifacts) {
  if (artifacts == 0) return;
  free(artifacts->config_header);
  free(artifacts->selection_json);
  free(artifacts->report_json);
  free(artifacts->inputs_json);
  free(artifacts->config_mk);
  free(artifacts->config_values_mk);
  free(artifacts->components_mk);
  free(artifacts->component_catalog_json);
  free(artifacts->bundle_json);
  memset(artifacts, 0, sizeof(*artifacts));
}

static int confit_v3_directory_exists(const char *path) {
  struct stat state;
  return stat(path, &state) == 0 && S_ISDIR(state.st_mode);
}

static int confit_v3_make_directory_once(const char *path) {
#if defined(_WIN32)
  return _mkdir(path) == 0;
#else
  return mkdir(path, 0755) == 0;
#endif
}

static ConfitStatus confit_v3_make_unique_staging(
    const char *staging_parent, const char *bundle_digest, char *out,
    size_t out_size, ConfitDiagnostic *diagnostic) {
  size_t attempt;

  for (attempt = 0U; attempt < 1024U; ++attempt) {
    const int written = snprintf(out, out_size, "%s/%s.%04llu.staging",
                                 staging_parent, bundle_digest,
                                 (unsigned long long)attempt);
    if (written < 0 || (size_t)written >= out_size) break;
    if (confit_v3_make_directory_once(out)) return CONFIT_OK;
    if (errno != EEXIST) break;
  }
  confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, staging_parent,
                        0U, 0U, "failed to allocate private sealed-bundle staging directory");
  return CONFIT_ERR_GENERATION;
}

static ConfitStatus confit_v3_join_path(char *out, size_t out_size,
                                        const char *left, const char *right,
                                        ConfitDiagnostic *diagnostic) {
  return confit_host_path_join(out, out_size, left, right, diagnostic);
}

static ConfitStatus confit_v3_verify_text(const char *path, const char *text,
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

static ConfitStatus confit_v3_complete_texts(
    const ConfitV3ArtifactSet *artifacts, ConfitV3NamedText texts[9],
    ConfitDiagnostic *diagnostic) {
  if (artifacts == 0 || artifacts->bundle_digest[0] == '\0' ||
      strlen(artifacts->bundle_digest) != 64U || artifacts->config_header == 0 ||
      artifacts->selection_json == 0 || artifacts->report_json == 0 ||
      artifacts->inputs_json == 0 || artifacts->config_mk == 0 ||
      artifacts->config_values_mk == 0 || artifacts->components_mk == 0 ||
      artifacts->component_catalog_json == 0 || artifacts->bundle_json == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          "sealed publication requires the complete ABI v3 artifact set");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  texts[0] = (ConfitV3NamedText){"config.h", artifacts->config_header, {0}};
  texts[1] = (ConfitV3NamedText){"config.selection.json", artifacts->selection_json, {0}};
  texts[2] = (ConfitV3NamedText){"config.report.json", artifacts->report_json, {0}};
  texts[3] = (ConfitV3NamedText){"config.inputs.json", artifacts->inputs_json, {0}};
  texts[4] = (ConfitV3NamedText){"config.mk", artifacts->config_mk, {0}};
  texts[5] = (ConfitV3NamedText){"config.values.mk", artifacts->config_values_mk, {0}};
  texts[6] = (ConfitV3NamedText){"components.mk", artifacts->components_mk, {0}};
  texts[7] = (ConfitV3NamedText){"component.catalog.json", artifacts->component_catalog_json, {0}};
  texts[8] = (ConfitV3NamedText){"config.bundle.json", artifacts->bundle_json, {0}};
  return CONFIT_OK;
}

static ConfitStatus confit_v3_verify_generation(
    const char *generation, const ConfitV3NamedText texts[9],
    ConfitDiagnostic *diagnostic) {
  size_t index;
  ConfitStatus status = CONFIT_OK;
  char path[4096];

  for (index = 0U; status == CONFIT_OK && index < 9U; ++index) {
    status = confit_v3_join_path(path, sizeof(path), generation, texts[index].path,
                                 diagnostic);
    if (status == CONFIT_OK) status = confit_v3_verify_text(path, texts[index].text,
                                                            diagnostic);
  }
  return status;
}

static ConfitStatus confit_v3_write_staging(
    const char *staging, const ConfitV3NamedText texts[9],
    size_t fault_after_artifact, ConfitDiagnostic *diagnostic) {
  size_t index;
  ConfitStatus status = CONFIT_OK;
  char path[4096];

  for (index = 0U; status == CONFIT_OK && index < 9U; ++index) {
    status = confit_v3_join_path(path, sizeof(path), staging, texts[index].path,
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
  if (status == CONFIT_OK) status = confit_v3_verify_generation(staging, texts, diagnostic);
  return status;
}

static void confit_v3_mark_read_only(const char *generation,
                                     const ConfitV3NamedText texts[9]) {
#if !defined(_WIN32)
  char path[4096];
  size_t index;
  for (index = 0U; index < 9U; ++index) {
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

ConfitStatus confit_v3_publish_artifacts(
    const ConfitV3PublishOptions *options, const ConfitV3ArtifactSet *artifacts,
    size_t *out_changed_file_count, ConfitDiagnostic *diagnostic) {
  ConfitV3NamedText texts[9];
  ConfitStatus status;
  char generations[4096];
  char staging_parent[4096];
  char staging[4096];
  char generation[4096];
  char selected[4096];
  char selected_text[128];
  int selected_changed = 0;
  int published_new = 0;

  if (options == 0 || options->output_root == 0 || options->output_root[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          "sealed publication requires an output root");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  status = confit_v3_complete_texts(artifacts, texts, diagnostic);
  if (status != CONFIT_OK) return status;
  status = confit_host_make_directories(options->output_root, diagnostic);
  if (status == CONFIT_OK) status = confit_v3_join_path(
      generations, sizeof(generations), options->output_root, "generations", diagnostic);
  if (status == CONFIT_OK) status = confit_host_make_directories(generations, diagnostic);
  if (status == CONFIT_OK) status = confit_v3_join_path(
      staging_parent, sizeof(staging_parent), options->output_root, ".staging", diagnostic);
  if (status == CONFIT_OK) status = confit_host_make_directories(staging_parent, diagnostic);
  if (status == CONFIT_OK) status = confit_v3_join_path(
      generation, sizeof(generation), generations, artifacts->bundle_digest, diagnostic);
  if (status != CONFIT_OK) return status;

  if (confit_v3_directory_exists(generation)) {
    status = confit_v3_verify_generation(generation, texts, diagnostic);
  } else {
    status = confit_v3_make_unique_staging(staging_parent, artifacts->bundle_digest,
                                           staging, sizeof(staging), diagnostic);
    if (status == CONFIT_OK) status = confit_v3_write_staging(
        staging, texts, options->fault_after_artifact, diagnostic);
    if (status == CONFIT_OK) {
      if (rename(staging, generation) != 0) {
        if (confit_v3_directory_exists(generation)) {
          status = confit_v3_verify_generation(generation, texts, diagnostic);
        } else {
          confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, generation,
                                0U, 0U, "failed to atomically publish sealed generation");
          status = CONFIT_ERR_GENERATION;
        }
      } else {
        published_new = 1;
        confit_v3_mark_read_only(generation, texts);
      }
    }
  }
  if (status == CONFIT_OK) status = confit_v3_join_path(
      selected, sizeof(selected), options->output_root, "selected", diagnostic);
  if (status == CONFIT_OK &&
      snprintf(selected_text, sizeof(selected_text), "generations/%s\n",
               artifacts->bundle_digest) >= (int)sizeof(selected_text)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, selected, 0U, 0U,
                          "selected alias path is too long");
    status = CONFIT_ERR_INTERNAL;
  }
  if (status == CONFIT_OK) status = confit_host_write_text_file_if_changed_atomic(
      selected, selected_text, &selected_changed, diagnostic);
  if (out_changed_file_count != 0) {
    *out_changed_file_count = status == CONFIT_OK &&
                              (selected_changed != 0 || published_new != 0)
                                  ? 1U
                                  : 0U;
  }
  return status;
}
