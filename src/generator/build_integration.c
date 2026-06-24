#include "confit/generator.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/version.h"

typedef struct ConfitIntegrationBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitIntegrationBuilder;

static void confit_integration_builder_init(
    ConfitIntegrationBuilder *builder) {
  builder->text = 0;
  builder->size = 0U;
  builder->capacity = 0U;
}

static ConfitStatus confit_integration_reserve(
    ConfitIntegrationBuilder *builder, size_t additional_size) {
  size_t required_capacity;
  size_t new_capacity;
  char *new_text;

  required_capacity = builder->size + additional_size + 1U;
  if (required_capacity <= builder->capacity) {
    return CONFIT_OK;
  }

  new_capacity = builder->capacity == 0U ? 1024U : builder->capacity;
  while (new_capacity < required_capacity) {
    new_capacity *= 2U;
  }

  new_text = (char *)realloc(builder->text, new_capacity);
  if (new_text == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  builder->text = new_text;
  builder->capacity = new_capacity;
  return CONFIT_OK;
}

static ConfitStatus confit_integration_append(
    ConfitIntegrationBuilder *builder, const char *text) {
  const size_t text_size = strlen(text);
  ConfitStatus status;

  status = confit_integration_reserve(builder, text_size);
  if (status != CONFIT_OK) {
    return status;
  }

  memcpy(builder->text + builder->size, text, text_size);
  builder->size += text_size;
  builder->text[builder->size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_integration_append_char(
    ConfitIntegrationBuilder *builder, char value) {
  char text[2];

  text[0] = value;
  text[1] = '\0';
  return confit_integration_append(builder, text);
}

static int confit_integration_is_lower(char value) {
  return value >= 'a' && value <= 'z';
}

static int confit_integration_is_upper(char value) {
  return value >= 'A' && value <= 'Z';
}

static int confit_integration_is_digit(char value) {
  return value >= '0' && value <= '9';
}

static int confit_integration_is_ident(char value) {
  return confit_integration_is_lower(value) ||
         confit_integration_is_upper(value) ||
         confit_integration_is_digit(value);
}

static char confit_integration_to_upper(char value) {
  if (confit_integration_is_lower(value)) {
    return (char)(value - 'a' + 'A');
  }
  return value;
}

static ConfitStatus confit_integration_append_macro_fragment(
    ConfitIntegrationBuilder *builder, const char *text) {
  size_t index;
  int last_was_separator;

  if (text == 0 || text[0] == '\0') {
    return confit_integration_append(builder, "UNKNOWN");
  }

  last_was_separator = 1;
  for (index = 0U; text[index] != '\0'; ++index) {
    char value;
    ConfitStatus status;

    value = text[index];
    if (confit_integration_is_ident(value)) {
      status = confit_integration_append_char(
          builder, confit_integration_to_upper(value));
      if (status != CONFIT_OK) {
        return status;
      }
      last_was_separator = 0;
      continue;
    }

    if (!last_was_separator) {
      status = confit_integration_append_char(builder, '_');
      if (status != CONFIT_OK) {
        return status;
      }
      last_was_separator = 1;
    }
  }

  if (builder->size > 0U && builder->text[builder->size - 1U] == '_') {
    builder->size -= 1U;
    builder->text[builder->size] = '\0';
  }
  return CONFIT_OK;
}

static ConfitStatus confit_integration_append_escaped(
    ConfitIntegrationBuilder *builder, const char *text) {
  size_t index;
  ConfitStatus status;

  status = confit_integration_append(builder, "\"");
  if (status != CONFIT_OK) {
    return status;
  }

  if (text != 0) {
    for (index = 0U; text[index] != '\0'; ++index) {
      switch (text[index]) {
      case '"':
      case '\\':
        status = confit_integration_append(builder, "\\");
        if (status == CONFIT_OK) {
          status = confit_integration_append_char(builder, text[index]);
        }
        break;
      case '\n':
        status = confit_integration_append(builder, "\\n");
        break;
      case '\r':
        status = confit_integration_append(builder, "\\r");
        break;
      case '\t':
        status = confit_integration_append(builder, "\\t");
        break;
      default:
        status = confit_integration_append_char(builder, text[index]);
        break;
      }
      if (status != CONFIT_OK) {
        return status;
      }
    }
  }

  return confit_integration_append(builder, "\"");
}

static ConfitStatus confit_integration_append_escaped_range(
    ConfitIntegrationBuilder *builder, const char *text, size_t text_size) {
  size_t index;
  ConfitStatus status;

  status = confit_integration_append(builder, "\"");
  if (status != CONFIT_OK) {
    return status;
  }

  if (text != 0) {
    for (index = 0U; index < text_size; ++index) {
      switch (text[index]) {
      case '"':
      case '\\':
        status = confit_integration_append(builder, "\\");
        if (status == CONFIT_OK) {
          status = confit_integration_append_char(builder, text[index]);
        }
        break;
      case '\n':
        status = confit_integration_append(builder, "\\n");
        break;
      case '\r':
        status = confit_integration_append(builder, "\\r");
        break;
      case '\t':
        status = confit_integration_append(builder, "\\t");
        break;
      default:
        status = confit_integration_append_char(builder, text[index]);
        break;
      }
      if (status != CONFIT_OK) {
        return status;
      }
    }
  }

  return confit_integration_append(builder, "\"");
}

static const char *confit_integration_profile_name(
    const ConfitBuildIntegrationOptions *options) {
  return options != 0 && options->profile_name != 0 ? options->profile_name
                                                    : "unknown";
}

static const char *confit_integration_target_name(
    const ConfitBuildIntegrationOptions *options) {
  return options != 0 && options->target_name != 0 ? options->target_name
                                                   : "unknown";
}

static const char *confit_integration_header_path(
    const ConfitBuildIntegrationOptions *options) {
  return options != 0 && options->header_path != 0 ? options->header_path
                                                   : "config.h";
}

static const char *confit_integration_report_json_path(
    const ConfitBuildIntegrationOptions *options) {
  return options != 0 && options->report_json_path != 0
             ? options->report_json_path
             : "config.report.json";
}

static const char *confit_integration_explain_text_path(
    const ConfitBuildIntegrationOptions *options) {
  return options != 0 && options->explain_text_path != 0
             ? options->explain_text_path
             : "config.explain.txt";
}

static const char *confit_integration_graph_json_path(
    const ConfitBuildIntegrationOptions *options) {
  return options != 0 && options->graph_json_path != 0
             ? options->graph_json_path
             : "config.graph.json";
}

static const char *confit_integration_inputs_json_path(
    const ConfitBuildIntegrationOptions *options) {
  return options != 0 && options->inputs_json_path != 0
             ? options->inputs_json_path
             : "config.inputs.json";
}

static const char *confit_integration_cmake_path(void) {
  return "config.cmake";
}

static const char *confit_integration_legacy_qst_path(void) {
  return "config.qst";
}

static const ConfitOption *confit_integration_find_option(
    const ConfitProject *project, const char *option_id) {
  size_t index;

  if (project == 0 || option_id == 0) {
    return 0;
  }

  for (index = 0U; index < project->option_count; ++index) {
    if (project->options[index].id != 0 &&
        strcmp(project->options[index].id, option_id) == 0) {
      return &project->options[index];
    }
  }
  return 0;
}

static ConfitStatus confit_integration_append_indent(
    ConfitIntegrationBuilder *builder, size_t count) {
  ConfitStatus status;
  size_t index;

  status = CONFIT_OK;
  for (index = 0U; status == CONFIT_OK && index < count; ++index) {
    status = confit_integration_append_char(builder, ' ');
  }
  return status;
}

static int confit_integration_is_lua_ident_start(char value) {
  return confit_integration_is_lower(value) ||
         confit_integration_is_upper(value) || value == '_';
}

static int confit_integration_is_lua_ident_continue(char value) {
  return confit_integration_is_lua_ident_start(value) ||
         confit_integration_is_digit(value);
}

static int confit_integration_is_lua_identifier(const char *text) {
  size_t index;

  if (text == 0 || !confit_integration_is_lua_ident_start(text[0])) {
    return 0;
  }
  for (index = 1U; text[index] != '\0'; ++index) {
    if (!confit_integration_is_lua_ident_continue(text[index])) {
      return 0;
    }
  }
  return 1;
}

static ConfitStatus confit_qstar_append_lua_key(
    ConfitIntegrationBuilder *builder, const char *key) {
  ConfitStatus status;

  if (confit_integration_is_lua_identifier(key)) {
    return confit_integration_append(builder, key);
  }

  status = confit_integration_append(builder, "[");
  if (status == CONFIT_OK) {
    status = confit_integration_append_escaped(builder, key);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, "]");
  }
  return status;
}

static int confit_integration_has_suffix(const char *text,
                                         const char *suffix) {
  size_t text_size;
  size_t suffix_size;

  if (text == 0 || suffix == 0) {
    return 0;
  }
  text_size = strlen(text);
  suffix_size = strlen(suffix);
  if (suffix_size > text_size) {
    return 0;
  }
  return strcmp(text + text_size - suffix_size, suffix) == 0;
}

static int confit_qstar_selection_field_is_list(const char *name) {
  return name != 0 &&
         (strcmp(name, "objects") == 0 ||
          strcmp(name, "include_dirs") == 0 ||
          confit_integration_has_suffix(name, "_dirs") ||
          confit_integration_has_suffix(name, "_paths") ||
          confit_integration_has_suffix(name, "_labels"));
}

static ConfitStatus confit_integration_source_hash(
    const ConfitResolvedConfig *config, uint64_t *out_hash,
    ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  *out_hash = 0U;
  status = confit_resolved_config_hash(config, out_hash);
  if (status != CONFIT_OK) {
    confit_diagnostic_set(diagnostic, status, 0, 0, 0,
                          "failed to hash resolved config");
  }
  return status;
}

static ConfitStatus confit_cmake_append_set(
    ConfitIntegrationBuilder *builder, const char *name, const char *value) {
  ConfitStatus status;

  status = confit_integration_append(builder, "set(");
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, name);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, " ");
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append_escaped(builder, value);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, ")\n");
  }
  return status;
}

static ConfitStatus confit_cmake_append_artifact_set(
    ConfitIntegrationBuilder *builder, const char *name, const char *path) {
  ConfitStatus status;

  status = confit_integration_append(builder, "set(");
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, name);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, " ");
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, "\"${CMAKE_CURRENT_LIST_DIR}/");
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, path);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, "\")\n");
  }
  return status;
}

static ConfitStatus confit_cmake_append_resolved_value_record(
    ConfitIntegrationBuilder *builder, const ConfitProject *project,
    const ConfitResolvedValue *resolved_value, ConfitDiagnostic *diagnostic) {
  const ConfitOption *option;
  char *record_text;
  ConfitStatus status;

  option = confit_integration_find_option(project, resolved_value->option_id);
  if (option == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                          resolved_value->option_id, 0, 0,
                          "resolved option is missing from project schema");
    return CONFIT_ERR_SCHEMA;
  }

  record_text = 0;
  status = confit_generator_serialize_resolved_value(
      resolved_value, option->type, CONFIT_GENERATOR_VALUE_CMAKE, &record_text,
      diagnostic);
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, record_text);
  }
  confit_generator_string_free(record_text);
  return status;
}

ConfitStatus confit_generate_cmake_fragment(
    const ConfitProject *project, const ConfitResolvedConfig *config,
    const ConfitBuildIntegrationOptions *options, char **out_text,
    ConfitDiagnostic *diagnostic) {
  ConfitIntegrationBuilder builder;
  uint64_t source_hash;
  char source_hash_text[32];
  char option_count_text[32];
  ConfitStatus status;
  size_t index;

  if (out_text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing cmake fragment output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_text = 0;
  if (project == 0 || project->name == 0 || config == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid cmake fragment generator argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_integration_source_hash(config, &source_hash, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  (void)snprintf(source_hash_text, sizeof(source_hash_text), "0x%llX",
                 (unsigned long long)source_hash);
  (void)snprintf(option_count_text, sizeof(option_count_text), "%lu",
                 (unsigned long)config->value_count);

  confit_integration_builder_init(&builder);

#define CONFIT_CMAKE_APPEND(fragment)                                           \
  do {                                                                          \
    status = confit_integration_append(&builder, (fragment));                   \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_CMAKE_SECTION(call_expr)                                         \
  do {                                                                          \
    status = (call_expr);                                                       \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_CMAKE_APPEND("# Generated by Confit. Do not edit.\n");
  CONFIT_CMAKE_APPEND("# This fragment is safe to include from a build tree.\n\n");
  CONFIT_CMAKE_SECTION(
      confit_cmake_append_set(&builder, "CONFIT_PROJECT", project->name));
  CONFIT_CMAKE_SECTION(confit_cmake_append_set(
      &builder, "CONFIT_PROFILE", confit_integration_profile_name(options)));
  CONFIT_CMAKE_SECTION(confit_cmake_append_set(
      &builder, "CONFIT_TARGET", confit_integration_target_name(options)));
  CONFIT_CMAKE_SECTION(confit_cmake_append_set(
      &builder, "CONFIT_VERSION", confit_version_string()));
  CONFIT_CMAKE_SECTION(
      confit_cmake_append_set(&builder, "CONFIT_SOURCE_HASH",
                              source_hash_text));
  CONFIT_CMAKE_SECTION(
      confit_cmake_append_set(&builder, "CONFIT_OPTION_COUNT",
                              option_count_text));
  CONFIT_CMAKE_APPEND("\n");
  CONFIT_CMAKE_SECTION(confit_cmake_append_artifact_set(
      &builder, "CONFIT_CONFIG_HEADER",
      confit_integration_header_path(options)));
  CONFIT_CMAKE_SECTION(confit_cmake_append_artifact_set(
      &builder, "CONFIT_CONFIG_REPORT_JSON",
      confit_integration_report_json_path(options)));
  CONFIT_CMAKE_SECTION(confit_cmake_append_artifact_set(
      &builder, "CONFIT_CONFIG_EXPLAIN_TXT",
      confit_integration_explain_text_path(options)));
  CONFIT_CMAKE_SECTION(confit_cmake_append_artifact_set(
      &builder, "CONFIT_CONFIG_GRAPH_JSON",
      confit_integration_graph_json_path(options)));
  CONFIT_CMAKE_SECTION(confit_cmake_append_artifact_set(
      &builder, "CONFIT_CONFIG_INPUTS_JSON",
      confit_integration_inputs_json_path(options)));
  CONFIT_CMAKE_APPEND("\nset(");
  CONFIT_CMAKE_SECTION(confit_integration_append_macro_fragment(
      &builder, project->name));
  CONFIT_CMAKE_APPEND("_CONFIG_HEADER \"${CONFIT_CONFIG_HEADER}\")\n");
  CONFIT_CMAKE_APPEND("set(");
  CONFIT_CMAKE_SECTION(confit_integration_append_macro_fragment(
      &builder, project->name));
  CONFIT_CMAKE_APPEND("_CONFIG_SOURCE_HASH \"${CONFIT_SOURCE_HASH}\")\n");
  CONFIT_CMAKE_APPEND("\n# Resolved option values.\n");
  for (index = 0U; index < config->value_count; ++index) {
    CONFIT_CMAKE_SECTION(confit_cmake_append_resolved_value_record(
        &builder, project, &config->values[index], diagnostic));
  }

#undef CONFIT_CMAKE_APPEND
#undef CONFIT_CMAKE_SECTION

  *out_text = builder.text;
  return CONFIT_OK;
}

static ConfitStatus confit_qstar_append_key_value(
    ConfitIntegrationBuilder *builder, const char *key, const char *value,
    int comma) {
  ConfitStatus status;

  status = confit_integration_append(builder, "  ");
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, key);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, " = ");
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append_escaped(builder, value);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, comma ? ",\n" : "\n");
  }
  return status;
}

static ConfitStatus confit_qstar_append_key_raw(
    ConfitIntegrationBuilder *builder, const char *key, const char *value,
    int comma) {
  ConfitStatus status;

  status = confit_integration_append(builder, "  ");
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, key);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, " = ");
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, value);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, comma ? ",\n" : "\n");
  }
  return status;
}

static ConfitStatus confit_qstar_append_artifact(
    ConfitIntegrationBuilder *builder, const char *key, const char *value,
    int comma) {
  ConfitStatus status;

  status = confit_integration_append(builder, "    ");
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, key);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, " = ");
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append_escaped(builder, value);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, comma ? ",\n" : "\n");
  }
  return status;
}

static ConfitStatus confit_qstar_append_value_entry(
    ConfitIntegrationBuilder *builder, const ConfitProject *project,
    const ConfitResolvedValue *resolved_value, int comma,
    ConfitDiagnostic *diagnostic) {
  const ConfitOption *option;
  char *entry_text;
  ConfitStatus status;

  option = confit_integration_find_option(project, resolved_value->option_id);
  if (option == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                          resolved_value->option_id, 0, 0,
                          "resolved option is missing from project schema");
    return CONFIT_ERR_SCHEMA;
  }

  entry_text = 0;
  status = confit_generator_serialize_resolved_value(
      resolved_value, option->type, CONFIT_GENERATOR_VALUE_LUA, &entry_text,
      diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  status = confit_integration_append(builder, "    [");
  if (status == CONFIT_OK) {
    status = confit_integration_append_escaped(builder,
                                               resolved_value->option_id);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, "] = ");
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, entry_text);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, comma ? ",\n" : "\n");
  }

  confit_generator_string_free(entry_text);
  return status;
}

ConfitStatus confit_generate_qstar_config_module(
    const ConfitProject *project, const ConfitResolvedConfig *config,
    const ConfitBuildIntegrationOptions *options, char **out_text,
    ConfitDiagnostic *diagnostic) {
  ConfitIntegrationBuilder builder;
  uint64_t source_hash;
  char source_hash_text[32];
  char option_count_text[32];
  ConfitStatus status;
  size_t index;

  if (out_text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing qstar config module output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_text = 0;
  if (project == 0 || project->name == 0 || config == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid qstar config module generator argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_integration_source_hash(config, &source_hash, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  (void)snprintf(source_hash_text, sizeof(source_hash_text), "0x%llX",
                 (unsigned long long)source_hash);
  (void)snprintf(option_count_text, sizeof(option_count_text), "%lu",
                 (unsigned long)config->value_count);

  confit_integration_builder_init(&builder);

#define CONFIT_QSM_SECTION(call_expr)                                           \
  do {                                                                          \
    status = (call_expr);                                                       \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_QSM_SECTION(
      confit_integration_append(&builder,
                                "-- Generated by Confit. Do not edit.\n"));
  CONFIT_QSM_SECTION(confit_integration_append(
      &builder,
      "-- Pure module for qstar.import_module(\".../config\").\n\n"));
  CONFIT_QSM_SECTION(confit_integration_append(&builder, "return {\n"));
  CONFIT_QSM_SECTION(confit_qstar_append_key_value(
      &builder, "schema", "confit-config-manifest-v1", 1));
  CONFIT_QSM_SECTION(
      confit_qstar_append_key_value(&builder, "project", project->name, 1));
  CONFIT_QSM_SECTION(confit_qstar_append_key_value(
      &builder, "profile", confit_integration_profile_name(options), 1));
  CONFIT_QSM_SECTION(confit_qstar_append_key_value(
      &builder, "target", confit_integration_target_name(options), 1));
  CONFIT_QSM_SECTION(confit_qstar_append_key_value(
      &builder, "confit_version", confit_version_string(), 1));
  CONFIT_QSM_SECTION(confit_qstar_append_key_value(
      &builder, "source_hash", source_hash_text, 1));
  CONFIT_QSM_SECTION(confit_qstar_append_key_raw(
      &builder, "option_count", option_count_text, 1));
  CONFIT_QSM_SECTION(confit_integration_append(&builder, "  artifacts = {\n"));
  CONFIT_QSM_SECTION(confit_qstar_append_artifact(
      &builder, "header", confit_integration_header_path(options), 1));
  CONFIT_QSM_SECTION(confit_qstar_append_artifact(
      &builder, "report_json", confit_integration_report_json_path(options),
      1));
  CONFIT_QSM_SECTION(confit_qstar_append_artifact(
      &builder, "explain_text", confit_integration_explain_text_path(options),
      1));
  CONFIT_QSM_SECTION(confit_qstar_append_artifact(
      &builder, "graph_json", confit_integration_graph_json_path(options),
      1));
  CONFIT_QSM_SECTION(confit_qstar_append_artifact(
      &builder, "inputs_json", confit_integration_inputs_json_path(options),
      1));
  CONFIT_QSM_SECTION(confit_qstar_append_artifact(
      &builder, "cmake", confit_integration_cmake_path(), 1));
  CONFIT_QSM_SECTION(confit_qstar_append_artifact(
      &builder, "legacy_qst", confit_integration_legacy_qst_path(), 0));
  CONFIT_QSM_SECTION(confit_integration_append(&builder, "  },\n"));
  CONFIT_QSM_SECTION(confit_integration_append(&builder, "  values = {\n"));
  for (index = 0U; index < config->value_count; ++index) {
    CONFIT_QSM_SECTION(confit_qstar_append_value_entry(
        &builder, project, &config->values[index],
        index + 1U < config->value_count, diagnostic));
  }
  CONFIT_QSM_SECTION(confit_integration_append(&builder, "  }\n"));
  CONFIT_QSM_SECTION(confit_integration_append(&builder, "}\n"));

#undef CONFIT_QSM_SECTION

  *out_text = builder.text;
  return CONFIT_OK;
}

static const char *confit_qstar_selection_string_payload(
    const ConfitValue *value) {
  if (value == 0) {
    return 0;
  }
  if (value->kind == CONFIT_VALUE_STRING ||
      value->kind == CONFIT_VALUE_ENUM ||
      value->kind == CONFIT_VALUE_PATH) {
    return value->as.string_value != 0 ? value->as.string_value : "";
  }
  return 0;
}

static ConfitStatus confit_qstar_append_selection_list_value(
    ConfitIntegrationBuilder *builder, const char *text, size_t indent) {
  size_t begin;
  size_t index;
  ConfitStatus status;

  status = confit_integration_append(builder, "{\n");
  if (status != CONFIT_OK) {
    return status;
  }

  begin = 0U;
  index = 0U;
  while (text != 0 && text[index] != '\0') {
    if (text[index] == ';') {
      if (index > begin) {
        status = confit_integration_append_indent(builder, indent + 2U);
        if (status == CONFIT_OK) {
          status = confit_integration_append_escaped_range(
              builder, text + begin, index - begin);
        }
        if (status == CONFIT_OK) {
          status = confit_integration_append(builder, ",\n");
        }
        if (status != CONFIT_OK) {
          return status;
        }
      }
      begin = index + 1U;
    }
    index += 1U;
  }

  if (text != 0 && index > begin) {
    status = confit_integration_append_indent(builder, indent + 2U);
    if (status == CONFIT_OK) {
      status =
          confit_integration_append_escaped_range(builder, text + begin,
                                                  index - begin);
    }
    if (status == CONFIT_OK) {
      status = confit_integration_append(builder, ",\n");
    }
    if (status != CONFIT_OK) {
      return status;
    }
  }

  status = confit_integration_append_indent(builder, indent);
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, "}");
  }
  return status;
}

static ConfitStatus confit_qstar_append_selection_field(
    ConfitIntegrationBuilder *builder, const ConfitProject *project,
    const ConfitResolvedConfig *config, const ConfitBuildSelectionField *field,
    size_t indent, ConfitDiagnostic *diagnostic) {
  const ConfitResolvedValue *resolved_value;
  const ConfitOption *option;
  const char *list_text;
  char *value_text;
  ConfitStatus status;

  resolved_value = confit_resolved_config_find(config, field->option_id);
  if (resolved_value == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_GENERATION, field->option_id,
                          0, 0,
                          "selection option is missing from resolved config");
    return CONFIT_ERR_GENERATION;
  }

  option = confit_integration_find_option(project, field->option_id);
  if (option == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, field->option_id, 0,
                          0,
                          "selection option is missing from project schema");
    return CONFIT_ERR_SCHEMA;
  }

  status = confit_integration_append_indent(builder, indent);
  if (status == CONFIT_OK) {
    status = confit_qstar_append_lua_key(builder, field->name);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, " = ");
  }
  if (status != CONFIT_OK) {
    return status;
  }

  list_text = confit_qstar_selection_field_is_list(field->name)
                  ? confit_qstar_selection_string_payload(
                        &resolved_value->value)
                  : 0;
  if (list_text != 0) {
    status = confit_qstar_append_selection_list_value(builder, list_text,
                                                      indent);
  } else {
    value_text = 0;
    status = confit_generator_serialize_value(
        &resolved_value->value, option->type, CONFIT_GENERATOR_VALUE_LUA,
        &value_text, diagnostic);
    if (status == CONFIT_OK) {
      status = confit_integration_append(builder, value_text);
    }
    confit_generator_string_free(value_text);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, ",\n");
  }
  return status;
}

static ConfitStatus confit_qstar_append_selection_section(
    ConfitIntegrationBuilder *builder, const ConfitProject *project,
    const ConfitResolvedConfig *config,
    const ConfitBuildSelectionSection *section, int comma,
    ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  size_t index;

  status = confit_integration_append(builder, "  ");
  if (status == CONFIT_OK) {
    status = confit_qstar_append_lua_key(builder, section->name);
  }
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, " = {\n");
  }
  if (status != CONFIT_OK) {
    return status;
  }

  for (index = 0U; index < section->field_count; ++index) {
    status = confit_qstar_append_selection_field(
        builder, project, config, &section->fields[index], 4U, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }

  status = confit_integration_append(builder, "  }");
  if (status == CONFIT_OK) {
    status = confit_integration_append(builder, comma ? ",\n" : "\n");
  }
  return status;
}

ConfitStatus confit_generate_build_selection_qsm(
    const ConfitProject *project, const ConfitResolvedConfig *config,
    const ConfitBuildIntegrationOptions *options,
    const ConfitBuildSelectionTemplate *selection, char **out_text,
    ConfitDiagnostic *diagnostic) {
  ConfitIntegrationBuilder builder;
  uint64_t source_hash;
  char source_hash_text[32];
  char schema_text[256];
  ConfitStatus status;
  size_t index;

  if (out_text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing build selection module output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_text = 0;
  if (project == 0 || project->name == 0 || config == 0 ||
      selection == 0 || selection->output == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid build selection module generator argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_integration_source_hash(config, &source_hash, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  (void)snprintf(source_hash_text, sizeof(source_hash_text), "0x%llX",
                 (unsigned long long)source_hash);
  (void)snprintf(schema_text, sizeof(schema_text), "%s-build-selection-v1",
                 project->name);

  confit_integration_builder_init(&builder);

#define CONFIT_SELECTION_QSM_SECTION(call_expr)                                \
  do {                                                                         \
    status = (call_expr);                                                      \
    if (status != CONFIT_OK) {                                                 \
      free(builder.text);                                                      \
      return status;                                                           \
    }                                                                          \
  } while (0)

  CONFIT_SELECTION_QSM_SECTION(confit_integration_append(
      &builder, "-- Generated by Confit. Do not edit.\n"));
  CONFIT_SELECTION_QSM_SECTION(confit_integration_append(
      &builder,
      "-- Pure project build selection module for qstar.import_module.\n\n"));
  CONFIT_SELECTION_QSM_SECTION(confit_integration_append(&builder,
                                                         "return {\n"));
  CONFIT_SELECTION_QSM_SECTION(
      confit_qstar_append_key_value(&builder, "schema", schema_text, 1));
  CONFIT_SELECTION_QSM_SECTION(
      confit_qstar_append_key_value(&builder, "project", project->name, 1));
  CONFIT_SELECTION_QSM_SECTION(confit_qstar_append_key_value(
      &builder, "profile", confit_integration_profile_name(options), 1));
  CONFIT_SELECTION_QSM_SECTION(confit_qstar_append_key_value(
      &builder, "target", confit_integration_target_name(options), 1));
  CONFIT_SELECTION_QSM_SECTION(confit_qstar_append_key_value(
      &builder, "source_hash", source_hash_text, 1));
  CONFIT_SELECTION_QSM_SECTION(confit_integration_append(&builder, "\n"));

  for (index = 0U; index < selection->section_count; ++index) {
    CONFIT_SELECTION_QSM_SECTION(confit_qstar_append_selection_section(
        &builder, project, config, &selection->sections[index],
        index + 1U < selection->section_count, diagnostic));
    if (index + 1U < selection->section_count) {
      CONFIT_SELECTION_QSM_SECTION(
          confit_integration_append(&builder, "\n"));
    }
  }
  CONFIT_SELECTION_QSM_SECTION(confit_integration_append(&builder, "}\n"));

#undef CONFIT_SELECTION_QSM_SECTION

  *out_text = builder.text;
  return CONFIT_OK;
}

ConfitStatus confit_generate_qstar_manifest(
    const ConfitProject *project, const ConfitResolvedConfig *config,
    const ConfitBuildIntegrationOptions *options, char **out_text,
    ConfitDiagnostic *diagnostic) {
  ConfitIntegrationBuilder builder;
  uint64_t source_hash;
  char source_hash_text[32];
  char option_count_text[32];
  ConfitStatus status;

  if (out_text == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing qstar manifest output pointer");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_text = 0;
  if (project == 0 || project->name == 0 || config == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid qstar manifest generator argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_integration_source_hash(config, &source_hash, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  (void)snprintf(source_hash_text, sizeof(source_hash_text), "0x%llX",
                 (unsigned long long)source_hash);
  (void)snprintf(option_count_text, sizeof(option_count_text), "%lu",
                 (unsigned long)config->value_count);

  confit_integration_builder_init(&builder);

#define CONFIT_QSTAR_SECTION(call_expr)                                         \
  do {                                                                          \
    status = (call_expr);                                                       \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_QSTAR_SECTION(
      confit_integration_append(&builder,
                                "-- Generated by Confit. Do not edit.\n"));
  CONFIT_QSTAR_SECTION(confit_integration_append(
      &builder,
      "-- This manifest is produced under the explicit --out directory.\n\n"));
  CONFIT_QSTAR_SECTION(confit_integration_append(&builder, "return {\n"));
  CONFIT_QSTAR_SECTION(confit_qstar_append_key_value(
      &builder, "schema", "confit-qstar-manifest-v1", 1));
  CONFIT_QSTAR_SECTION(
      confit_qstar_append_key_value(&builder, "project", project->name, 1));
  CONFIT_QSTAR_SECTION(confit_qstar_append_key_value(
      &builder, "profile", confit_integration_profile_name(options), 1));
  CONFIT_QSTAR_SECTION(confit_qstar_append_key_value(
      &builder, "target", confit_integration_target_name(options), 1));
  CONFIT_QSTAR_SECTION(confit_qstar_append_key_value(
      &builder, "confit_version", confit_version_string(), 1));
  CONFIT_QSTAR_SECTION(confit_qstar_append_key_value(
      &builder, "source_hash", source_hash_text, 1));
  CONFIT_QSTAR_SECTION(confit_qstar_append_key_value(
      &builder, "option_count", option_count_text, 1));
  CONFIT_QSTAR_SECTION(confit_integration_append(&builder, "  artifacts = {\n"));
  CONFIT_QSTAR_SECTION(confit_qstar_append_artifact(
      &builder, "header", confit_integration_header_path(options), 1));
  CONFIT_QSTAR_SECTION(confit_qstar_append_artifact(
      &builder, "report_json", confit_integration_report_json_path(options),
      1));
  CONFIT_QSTAR_SECTION(confit_qstar_append_artifact(
      &builder, "explain_text", confit_integration_explain_text_path(options),
      1));
  CONFIT_QSTAR_SECTION(confit_qstar_append_artifact(
      &builder, "graph_json", confit_integration_graph_json_path(options),
      1));
  CONFIT_QSTAR_SECTION(confit_qstar_append_artifact(
      &builder, "inputs_json", confit_integration_inputs_json_path(options),
      0));
  CONFIT_QSTAR_SECTION(confit_integration_append(&builder, "  }\n"));
  CONFIT_QSTAR_SECTION(confit_integration_append(&builder, "}\n"));

#undef CONFIT_QSTAR_SECTION

  *out_text = builder.text;
  return CONFIT_OK;
}
