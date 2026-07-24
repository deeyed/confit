#include "confit/migration_v2.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/host.h"
#include "confit/model.h"
#include "confit/schema.h"

typedef struct ConfitMigrationBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitMigrationBuilder;

static const char kInvalidArgument[] = "invalid v1 to v2 migration argument";
static const char kSameOutput[] =
    "migration output must be separate from the source project";

static int confit_migration_path_is_same_or_child(const char *root,
                                                   const char *path) {
  size_t root_size;

  if (root == 0 || path == 0) return 0;
  root_size = strlen(root);
  return strcmp(root, path) == 0 ||
         (strncmp(root, path, root_size) == 0 &&
          (path[root_size] == '/' || path[root_size] == '\\'));
}

static void confit_migration_builder_init(ConfitMigrationBuilder *builder) {
  memset(builder, 0, sizeof(*builder));
}

static void confit_migration_builder_clear(ConfitMigrationBuilder *builder) {
  free(builder->text);
  memset(builder, 0, sizeof(*builder));
}

static ConfitStatus confit_migration_builder_reserve(
    ConfitMigrationBuilder *builder, size_t extra) {
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

static ConfitStatus confit_migration_builder_append_n(
    ConfitMigrationBuilder *builder, const char *text, size_t size) {
  ConfitStatus status = confit_migration_builder_reserve(builder, size);

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

static ConfitStatus confit_migration_builder_append(
    ConfitMigrationBuilder *builder, const char *text) {
  return confit_migration_builder_append_n(builder, text, strlen(text));
}

static ConfitStatus confit_migration_builder_appendf(
    ConfitMigrationBuilder *builder, const char *format, ...) {
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
  status = confit_migration_builder_reserve(builder, (size_t)written);
  if (status == CONFIT_OK) {
    (void)vsnprintf(builder->text + builder->size,
                    builder->capacity - builder->size, format, arguments);
    builder->size += (size_t)written;
  }
  va_end(arguments);
  return status;
}

static ConfitStatus confit_migration_append_toml_string(
    ConfitMigrationBuilder *builder, const char *text) {
  size_t index;
  ConfitStatus status = confit_migration_builder_append(builder, "\"");

  for (index = 0U; status == CONFIT_OK && text != 0 && text[index] != '\0';
       ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (value == '"' || value == '\\') {
      status = confit_migration_builder_appendf(builder, "\\%c", value);
    } else if (value == '\n') {
      status = confit_migration_builder_append(builder, "\\n");
    } else if (value == '\r') {
      status = confit_migration_builder_append(builder, "\\r");
    } else if (value == '\t') {
      status = confit_migration_builder_append(builder, "\\t");
    } else if (value < 0x20U) {
      status = confit_migration_builder_appendf(builder, "\\u%04x", value);
    } else {
      status = confit_migration_builder_append_n(builder, (const char *)&value,
                                                 1U);
    }
  }
  return status == CONFIT_OK ? confit_migration_builder_append(builder, "\"")
                             : status;
}

static ConfitStatus confit_migration_append_json_string(
    ConfitMigrationBuilder *builder, const char *text) {
  return confit_migration_append_toml_string(builder, text);
}

static const char *confit_migration_v2_type(ConfitOptionType type) {
  switch (type) {
  case CONFIT_OPTION_TYPE_BOOL: return "bool";
  case CONFIT_OPTION_TYPE_INT: return "int";
  case CONFIT_OPTION_TYPE_UINT: return "uint";
  case CONFIT_OPTION_TYPE_HEX: return "hex";
  case CONFIT_OPTION_TYPE_STRING: return "string";
  case CONFIT_OPTION_TYPE_ENUM: return "enum";
  case CONFIT_OPTION_TYPE_FLOAT: return "float";
  case CONFIT_OPTION_TYPE_PATH: return "path";
  default: return "invalid";
  }
}

static ConfitStatus confit_migration_append_value(
    ConfitMigrationBuilder *builder, const ConfitValue *value) {
  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return confit_migration_builder_append(
        builder, value->as.bool_value ? "true" : "false");
  case CONFIT_VALUE_INT:
    return confit_migration_builder_appendf(builder, "%lld",
                                            (long long)value->as.int_value);
  case CONFIT_VALUE_UINT:
    return confit_migration_builder_appendf(builder, "%llu",
                                            (unsigned long long)value->as.uint_value);
  case CONFIT_VALUE_FLOAT:
    return confit_migration_builder_appendf(builder, "%.17g", value->as.float_value);
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    return confit_migration_append_toml_string(builder, value->as.string_value);
  case CONFIT_VALUE_EMPTY:
  default:
    return CONFIT_ERR_SCHEMA;
  }
}

static ConfitStatus confit_migration_append_string_array(
    ConfitMigrationBuilder *builder, char *const *items, size_t item_count) {
  size_t index;
  ConfitStatus status = confit_migration_builder_append(builder, "[");

  for (index = 0U; status == CONFIT_OK && index < item_count; ++index) {
    if (index > 0U) {
      status = confit_migration_builder_append(builder, ", ");
    }
    if (status == CONFIT_OK) {
      status = confit_migration_append_toml_string(builder, items[index]);
    }
  }
  return status == CONFIT_OK ? confit_migration_builder_append(builder, "]")
                             : status;
}

static ConfitStatus confit_migration_namespace(const ConfitProject *project,
                                               char *out, size_t out_size,
                                               ConfitDiagnostic *diagnostic) {
  const char *id;
  const char *separator;
  size_t size;

  if (project == 0 || project->option_count == 0U || out == 0 ||
      out_size == 0U || project->options[0].id == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, 0, 0U, 0U,
                          "migration requires at least one namespaced option id");
    return CONFIT_ERR_SCHEMA;
  }
  id = project->options[0].id;
  separator = strchr(id, '.');
  if (separator == 0 || separator == id ||
      (size = (size_t)(separator - id)) + 1U > out_size) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, id, 0U, 0U,
                          "migration could not derive the v2 project namespace");
    return CONFIT_ERR_SCHEMA;
  }
  memcpy(out, id, size);
  out[size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_migration_append_options(
    ConfitMigrationBuilder *builder, const ConfitProject *project,
    size_t *out_dependency_todos, size_t *out_category_todos,
    size_t *out_force_todos) {
  size_t index;
  ConfitStatus status = CONFIT_OK;

  *out_dependency_todos = 0U;
  *out_category_todos = 0U;
  *out_force_todos = 0U;
  status = confit_migration_builder_append(builder, "schema_version = 2\n");
  for (index = 0U; status == CONFIT_OK && index < project->option_count;
       ++index) {
    const ConfitOption *option = &project->options[index];
    const char *owner = option->owner != 0 ? option->owner : "migration-todo";
    const char *since = option->since != 0 ? option->since : "0.0.0";
    const char *stability = option->stability != 0 ? option->stability : "experimental";

    if (option->type == CONFIT_OPTION_TYPE_INVALID ||
        option->default_value.kind == CONFIT_VALUE_EMPTY) {
      return CONFIT_ERR_SCHEMA;
    }
    status = confit_migration_builder_append(builder, "\n[option.");
    if (status == CONFIT_OK) status = confit_migration_append_toml_string(builder, option->id);
    if (status == CONFIT_OK) status = confit_migration_builder_appendf(
        builder, "]\ntype = \"%s\"\ndefault = ",
        confit_migration_v2_type(option->type));
    if (status == CONFIT_OK) status = confit_migration_append_value(builder, &option->default_value);
    if (status == CONFIT_OK) status = confit_migration_builder_append(
        builder, "\nwrite_domain = \"profile\"\nowner = ");
    if (status == CONFIT_OK) status = confit_migration_append_toml_string(builder, owner);
    if (status == CONFIT_OK) status = confit_migration_builder_append(builder, "\nsince = ");
    if (status == CONFIT_OK) status = confit_migration_append_toml_string(builder, since);
    if (status == CONFIT_OK) status = confit_migration_builder_append(builder, "\nstability = ");
    if (status == CONFIT_OK) status = confit_migration_append_toml_string(builder, stability);
    if (status == CONFIT_OK && option->prompt != 0) {
      status = confit_migration_builder_append(builder, "\nprompt = ");
      if (status == CONFIT_OK) status = confit_migration_append_toml_string(builder, option->prompt);
    }
    if (status == CONFIT_OK && option->help != 0) {
      status = confit_migration_builder_append(builder, "\nhelp = ");
      if (status == CONFIT_OK) status = confit_migration_append_toml_string(builder, option->help);
    }
    if (status == CONFIT_OK && option->tag_count > 0U) {
      status = confit_migration_builder_append(builder, "\ntags = ");
      if (status == CONFIT_OK) status = confit_migration_append_string_array(
          builder, option->tags, option->tag_count);
    }
    if (status == CONFIT_OK && option->enum_value_count > 0U) {
      status = confit_migration_builder_append(builder, "\nvalues = ");
      if (status == CONFIT_OK) status = confit_migration_append_string_array(
          builder, option->enum_values, option->enum_value_count);
    }
    if (status == CONFIT_OK && option->has_range) {
      status = confit_migration_builder_append(builder, "\nrange = { min = ");
      if (status == CONFIT_OK) status = confit_migration_append_value(builder, &option->range_min);
      if (status == CONFIT_OK) status = confit_migration_builder_append(builder, ", max = ");
      if (status == CONFIT_OK) status = confit_migration_append_value(builder, &option->range_max);
      if (status == CONFIT_OK) status = confit_migration_builder_append(builder, " }");
    }
    if (option->dependency_count > 0U) {
      size_t dependency_index;

      *out_dependency_todos += option->dependency_count;
      for (dependency_index = 0U; dependency_index < option->dependency_count;
           ++dependency_index) {
        if (option->dependencies[dependency_index].kind ==
            CONFIT_DEPENDENCY_FORCES) {
          *out_force_todos += 1U;
        }
      }
    }
    if (option->category != 0 && option->category[0] != '\0') {
      *out_category_todos += 1U;
    }
    if (status == CONFIT_OK) status = confit_migration_builder_append(builder, "\n");
  }
  return status;
}

static ConfitStatus confit_migration_write_file(const char *directory,
                                                 const char *name,
                                                 const char *text,
                                                 ConfitDiagnostic *diagnostic) {
  char path[4096];
  int changed;
  ConfitStatus status = confit_host_path_join(path, sizeof(path), directory, name,
                                              diagnostic);

  return status == CONFIT_OK
             ? confit_host_write_text_file_if_changed_atomic(path, text, &changed,
                                                              diagnostic)
             : status;
}

ConfitStatus confit_v1_migrate_to_v2_candidate(
    const ConfitV1ToV2MigrationOptions *options, ConfitDiagnostic *diagnostic) {
  ConfitProject *project = 0;
  ConfitMigrationBuilder project_toml;
  ConfitMigrationBuilder options_toml;
  ConfitMigrationBuilder report;
  ConfitMigrationBuilder manifest;
  char source_canonical[4096];
  char source_config_canonical[4096];
  char output_canonical[4096];
  char config_root[4096];
  char source_project_path[4096];
  char output_config_path[4096];
  char namespace_name[256];
  size_t dependency_todos;
  size_t category_todos;
  size_t force_todos;
  ConfitStatus status;
  ConfitDiagnostic ignored_diagnostic;

  if (options == 0 || options->source_project_root == 0 ||
      options->output_root == 0 || options->source_project_root[0] == '\0' ||
      options->output_root[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          kInvalidArgument);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  confit_migration_builder_init(&project_toml);
  confit_migration_builder_init(&options_toml);
  confit_migration_builder_init(&report);
  confit_migration_builder_init(&manifest);
  confit_diagnostic_init(&ignored_diagnostic);
  if (confit_migration_path_is_same_or_child(options->source_project_root,
                                              options->output_root)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                          options->output_root, 0U, 0U, kSameOutput);
    status = CONFIT_ERR_INVALID_ARGUMENT;
  } else {
    status = CONFIT_OK;
  }
  if (status == CONFIT_OK) {
    status = confit_host_path_canonicalize(source_canonical,
                                           sizeof(source_canonical),
                                           options->source_project_root,
                                           diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_path_join(source_project_path,
                                   sizeof(source_project_path),
                                   options->source_project_root,
                                   "project.toml", diagnostic);
  }
  if (status == CONFIT_OK && confit_host_file_exists(source_project_path)) {
    memcpy(source_config_canonical, source_canonical,
           strlen(source_canonical) + 1U);
  } else if (status == CONFIT_OK) {
    status = confit_host_path_join(source_config_canonical,
                                   sizeof(source_config_canonical),
                                   options->source_project_root, "config",
                                   diagnostic);
    if (status == CONFIT_OK) {
      status = confit_host_path_canonicalize(
          source_config_canonical, sizeof(source_config_canonical),
          source_config_canonical, diagnostic);
    }
  }
  if (status == CONFIT_OK &&
      confit_host_path_canonicalize(output_canonical, sizeof(output_canonical),
                                    options->output_root,
                                    &ignored_diagnostic) == CONFIT_OK) {
    status = confit_host_path_join(output_config_path,
                                   sizeof(output_config_path), output_canonical,
                                   "config", diagnostic);
    if (status == CONFIT_OK &&
        (confit_migration_path_is_same_or_child(source_canonical,
                                                 output_canonical) ||
         confit_migration_path_is_same_or_child(source_config_canonical,
                                                 output_canonical) ||
         strcmp(source_config_canonical, output_config_path) == 0)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                            options->output_root, 0U, 0U, kSameOutput);
      status = CONFIT_ERR_INVALID_ARGUMENT;
    }
  }
  if (status == CONFIT_OK) {
    status = confit_schema_load_project(options->source_project_root, &project,
                                        diagnostic);
  }
  if (status == CONFIT_OK && project->schema_version != 1U) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA,
                          options->source_project_root, 0U, 0U,
                          "migrate accepts schema_version = 1 sources only");
    status = CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK) {
    status = confit_migration_namespace(project, namespace_name,
                                         sizeof(namespace_name), diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_migration_builder_append(&project_toml, "[project]\nname = ");
    if (status == CONFIT_OK) status = confit_migration_append_toml_string(&project_toml, project->name);
    if (status == CONFIT_OK) status = confit_migration_builder_append(&project_toml, "\nnamespace = ");
    if (status == CONFIT_OK) status = confit_migration_append_toml_string(&project_toml, namespace_name);
    if (status == CONFIT_OK) status = confit_migration_builder_append(&project_toml, "\nversion = ");
    if (status == CONFIT_OK) status = confit_migration_append_toml_string(
        &project_toml, project->version != 0 ? project->version : "0.0.0");
    if (status == CONFIT_OK) status = confit_migration_builder_append(
        &project_toml, "\nschema_version = 2\nimports = [\"options.toml\"]\n");
  }
  if (status == CONFIT_OK) {
    status = confit_migration_append_options(&options_toml, project,
                                              &dependency_todos, &category_todos,
                                              &force_todos);
  }
  if (status == CONFIT_OK) {
    status = confit_migration_builder_append(
        &report,
        "{\n  \"schema\": \"confit-migration-report-v2\",\n"
        "  \"source_schema_version\": 1,\n  \"candidate_schema_version\": 2,\n"
        "  \"project\": ");
    if (status == CONFIT_OK) status = confit_migration_append_json_string(&report, project->name);
    if (status == CONFIT_OK) status = confit_migration_builder_appendf(
        &report,
        ",\n  \"converted\": {\"options\": %llu},\n"
        "  \"todo\": {\"dependencies\": %llu, \"categories\": %llu, "
        "\"profiles\": %llu, \"targets\": %llu, \"forces\": %llu, "
        "\"writer_conflicts\": 0},\n"
        "  \"policy\": \"candidate only; source tree was not modified\"\n}\n",
        (unsigned long long)project->option_count,
        (unsigned long long)dependency_todos, (unsigned long long)category_todos,
        (unsigned long long)project->profile_count,
        (unsigned long long)project->target_count,
        (unsigned long long)force_todos);
  }
  if (status == CONFIT_OK) {
    status = confit_migration_builder_append(
        &manifest,
        "{\n  \"schema\": \"confit-migration-input-manifest-v2\",\n"
        "  \"source_project\": ");
    if (status == CONFIT_OK) status = confit_migration_append_json_string(
        &manifest, options->source_project_root);
    if (status == CONFIT_OK) status = confit_migration_builder_append(
        &manifest,
        ",\n  \"source_schema_version\": 1,\n"
        "  \"semantic_comparison\": \"manual review required for TODO items\"\n}\n");
  }
  if (status == CONFIT_OK) {
    status = confit_host_path_join(config_root, sizeof(config_root),
                                   options->output_root, "config", diagnostic);
  }
  if (status == CONFIT_OK) status = confit_host_make_directories(config_root, diagnostic);
  if (status == CONFIT_OK) status = confit_migration_write_file(
      config_root, "project.toml", project_toml.text, diagnostic);
  if (status == CONFIT_OK) status = confit_migration_write_file(
      config_root, "options.toml", options_toml.text, diagnostic);
  if (status == CONFIT_OK) status = confit_migration_write_file(
      options->output_root, "migration-report.json", report.text, diagnostic);
  if (status == CONFIT_OK) status = confit_migration_write_file(
      options->output_root, "migration-inputs.json", manifest.text, diagnostic);

  confit_project_free(project);
  confit_migration_builder_clear(&project_toml);
  confit_migration_builder_clear(&options_toml);
  confit_migration_builder_clear(&report);
  confit_migration_builder_clear(&manifest);
  return status;
}
