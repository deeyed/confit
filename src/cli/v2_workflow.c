#include "v2_workflow.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/component_catalog.h"
#include "confit/constraint_v2.h"
#include "confit/generator_v2.h"
#include "confit/host.h"
#include "confit/parser_v2.h"
#include "confit/resolver_v2.h"
#include "confit/schema_v2.h"
#include "confit/status.h"
#include "confit/version.h"

typedef struct ConfitCliV2Builder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitCliV2Builder;

typedef struct ConfitCliV2Args {
  const char *project;
  const char *profile;
  const char *target;
  const char *out;
  const char *format;
  const char *artifact;
  const char *option_id;
  const char *base;
  const char *kind;
  const char *category;
  const char *tag;
  const char *query;
  const char *component_action;
  const char *component_id;
  const char **sets;
  size_t set_count;
  int strict;
  int force;
  int dry_run;
  int diagnostic_json;
} ConfitCliV2Args;

typedef struct ConfitCliV2Context {
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2Snapshot *snapshot;
  ConfitV2UserOverride *overrides;
  char **override_ids;
  size_t override_count;
} ConfitCliV2Context;

typedef struct ConfitCliV3InputSet {
  ConfitV2ArtifactInput *records;
  char **paths;
  char **hashes;
  char **roles;
  size_t count;
} ConfitCliV3InputSet;

static void confit_cli_v2_context_clear(ConfitCliV2Context *context);
static ConfitStatus confit_cli_v2_context_load(const ConfitCliV2Args *args,
                                               ConfitCliV2Context *context,
                                               ConfitDiagnostic *diagnostic);

static const char kInvalidCommand[] = "invalid schema v2 command arguments";
static const char kInvalidOption[] = "unknown schema v2 command option";
static const char kMissingProject[] = "schema v2 command requires --project";

static void confit_cli_v2_builder_init(ConfitCliV2Builder *builder) {
  memset(builder, 0, sizeof(*builder));
}

static void confit_cli_v2_builder_clear(ConfitCliV2Builder *builder) {
  free(builder->text);
  memset(builder, 0, sizeof(*builder));
}

static ConfitStatus confit_cli_v2_builder_reserve(ConfitCliV2Builder *builder,
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
  capacity = builder->capacity == 0U ? 512U : builder->capacity;
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

static ConfitStatus confit_cli_v2_builder_append_n(ConfitCliV2Builder *builder,
                                                    const char *text,
                                                    size_t size) {
  ConfitStatus status = confit_cli_v2_builder_reserve(builder, size);

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

static ConfitStatus confit_cli_v2_builder_append(ConfitCliV2Builder *builder,
                                                  const char *text) {
  return confit_cli_v2_builder_append_n(builder, text, strlen(text));
}

static ConfitStatus confit_cli_v2_builder_appendf(ConfitCliV2Builder *builder,
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
  status = confit_cli_v2_builder_reserve(builder, (size_t)written);
  if (status == CONFIT_OK) {
    (void)vsnprintf(builder->text + builder->size,
                    builder->capacity - builder->size, format, arguments);
    builder->size += (size_t)written;
  }
  va_end(arguments);
  return status;
}

static char *confit_cli_v2_builder_take(ConfitCliV2Builder *builder) {
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

static ConfitStatus confit_cli_v2_json_string(ConfitCliV2Builder *builder,
                                               const char *text) {
  size_t index;
  ConfitStatus status = confit_cli_v2_builder_append(builder, "\"");

  for (index = 0U; status == CONFIT_OK && text != 0 && text[index] != '\0';
       ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (value == '"' || value == '\\') {
      status = confit_cli_v2_builder_appendf(builder, "\\%c", value);
    } else if (value == '\n') {
      status = confit_cli_v2_builder_append(builder, "\\n");
    } else if (value == '\r') {
      status = confit_cli_v2_builder_append(builder, "\\r");
    } else if (value == '\t') {
      status = confit_cli_v2_builder_append(builder, "\\t");
    } else if (value < 0x20U) {
      status = confit_cli_v2_builder_appendf(builder, "\\u%04x", value);
    } else {
      status = confit_cli_v2_builder_append_n(builder, (const char *)&value, 1U);
    }
  }
  return status == CONFIT_OK ? confit_cli_v2_builder_append(builder, "\"")
                             : status;
}

static ConfitStatus confit_cli_v2_toml_string(ConfitCliV2Builder *builder,
                                               const char *text) {
  return confit_cli_v2_json_string(builder, text);
}

static const char *confit_cli_v2_type_name(ConfitV2OptionType type) {
  switch (type) {
  case CONFIT_V2_OPTION_TYPE_BOOL: return "bool";
  case CONFIT_V2_OPTION_TYPE_TRISTATE: return "tristate";
  case CONFIT_V2_OPTION_TYPE_INT: return "int";
  case CONFIT_V2_OPTION_TYPE_UINT: return "uint";
  case CONFIT_V2_OPTION_TYPE_HEX: return "hex";
  case CONFIT_V2_OPTION_TYPE_FLOAT: return "float";
  case CONFIT_V2_OPTION_TYPE_STRING: return "string";
  case CONFIT_V2_OPTION_TYPE_ENUM: return "enum";
  case CONFIT_V2_OPTION_TYPE_PATH: return "path";
  case CONFIT_V2_OPTION_TYPE_STRING_LIST: return "string_list";
  case CONFIT_V2_OPTION_TYPE_PATH_LIST: return "path_list";
  case CONFIT_V2_OPTION_TYPE_ENUM_SET: return "enum_set";
  default: return "invalid";
  }
}

static const char *confit_cli_v2_effective_origin_name(
    ConfitV2EffectiveValueOrigin origin) {
  switch (origin) {
  case CONFIT_V2_EFFECTIVE_VALUE_REQUESTED: return "requested";
  case CONFIT_V2_EFFECTIVE_VALUE_CONDITIONAL_DEFAULT: return "conditional_default";
  case CONFIT_V2_EFFECTIVE_VALUE_DEFAULT: return "default";
  case CONFIT_V2_EFFECTIVE_VALUE_COMPUTED: return "computed";
  case CONFIT_V2_EFFECTIVE_VALUE_UNSET: return "unset";
  default: return "unknown";
  }
}

static ConfitStatus confit_cli_v2_append_value(ConfitCliV2Builder *builder,
                                                const ConfitV2Value *value,
                                                int json) {
  size_t index;
  ConfitStatus status;

  if (value == 0 || value->kind == CONFIT_V2_VALUE_UNSET) {
    return confit_cli_v2_builder_append(builder, json ? "null" : "<unset>");
  }
  switch (value->kind) {
  case CONFIT_V2_VALUE_BOOL:
    return confit_cli_v2_builder_append(builder,
                                        value->as.bool_value ? "true" : "false");
  case CONFIT_V2_VALUE_TRISTATE:
    return confit_cli_v2_builder_appendf(builder, "%c", value->as.tristate_value);
  case CONFIT_V2_VALUE_INT:
    return confit_cli_v2_builder_appendf(builder, "%lld",
                                         (long long)value->as.int_value);
  case CONFIT_V2_VALUE_UINT:
    return confit_cli_v2_builder_appendf(builder, "%llu",
                                         (unsigned long long)value->as.uint_value);
  case CONFIT_V2_VALUE_FLOAT:
    return confit_cli_v2_builder_appendf(builder, "%.17g", value->as.float_value);
  case CONFIT_V2_VALUE_STRING:
    return json ? confit_cli_v2_json_string(builder, value->as.string_value)
                : confit_cli_v2_builder_append(builder, value->as.string_value);
  case CONFIT_V2_VALUE_STRING_LIST:
    status = confit_cli_v2_builder_append(builder, "[");
    for (index = 0U; status == CONFIT_OK && index < value->as.string_list.count;
         ++index) {
      if (index > 0U) status = confit_cli_v2_builder_append(builder, ", ");
      if (status == CONFIT_OK) {
        status = json ? confit_cli_v2_json_string(
                            builder, value->as.string_list.items[index])
                      : confit_cli_v2_builder_append(
                            builder, value->as.string_list.items[index]);
      }
    }
    return status == CONFIT_OK ? confit_cli_v2_builder_append(builder, "]")
                               : status;
  case CONFIT_V2_VALUE_UNSET:
  default:
    return confit_cli_v2_builder_append(builder, json ? "null" : "<unset>");
  }
}

static void confit_cli_v2_args_init(ConfitCliV2Args *args) {
  memset(args, 0, sizeof(*args));
  args->format = "text";
  args->artifact = "bundle";
  args->kind = "options";
}

static void confit_cli_v2_args_clear(ConfitCliV2Args *args) {
  free((void *)args->sets);
  memset(args, 0, sizeof(*args));
}

static ConfitStatus confit_cli_v2_add_set(ConfitCliV2Args *args,
                                           const char *assignment) {
  const char **grown = (const char **)realloc(
      (void *)args->sets, (args->set_count + 1U) * sizeof(*args->sets));
  if (grown == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  args->sets = grown;
  args->sets[args->set_count] = assignment;
  args->set_count += 1U;
  return CONFIT_OK;
}

static char *confit_cli_v2_strdup(const char *text) {
  const size_t size = strlen(text) + 1U;
  char *copy = (char *)malloc(size);
  if (copy != 0) memcpy(copy, text, size);
  return copy;
}

static void confit_cli_v3_input_set_clear(ConfitCliV3InputSet *inputs) {
  size_t index;
  if (inputs == 0) return;
  for (index = 0U; index < inputs->count; ++index) {
    free(inputs->paths[index]);
    free(inputs->hashes[index]);
    free(inputs->roles[index]);
  }
  free(inputs->records);
  free(inputs->paths);
  free(inputs->hashes);
  free(inputs->roles);
  memset(inputs, 0, sizeof(*inputs));
}

static ConfitStatus confit_cli_v3_input_set_add_text(
    ConfitCliV3InputSet *inputs, const char *path, const char *role,
    const char *text, ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactInput *records;
  char **paths;
  char **hashes;
  char **roles;
  char *path_copy;
  char *hash_copy;
  char *role_copy;
  char digest[65];
  char prefixed[72];
  size_t index;

  for (index = 0U; index < inputs->count; ++index) {
    if (strcmp(inputs->paths[index], path) == 0) {
      if (strcmp(inputs->roles[index], role) == 0) return CONFIT_OK;
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path, 0U, 0U,
                            "one sealed provenance path has conflicting roles");
      return CONFIT_ERR_SCHEMA;
    }
  }
  if (inputs->count == SIZE_MAX / sizeof(*inputs->records)) return CONFIT_ERR_INTERNAL;
  path_copy = confit_cli_v2_strdup(path);
  role_copy = confit_cli_v2_strdup(role);
  confit_v3_sha256_hex(text, digest);
  if (snprintf(prefixed, sizeof(prefixed), "sha256:%s", digest) < 0) {
    free(path_copy); free(role_copy);
    return CONFIT_ERR_INTERNAL;
  }
  hash_copy = confit_cli_v2_strdup(prefixed);
  if (path_copy == 0 || role_copy == 0 || hash_copy == 0) {
    free(path_copy); free(role_copy); free(hash_copy);
    return CONFIT_ERR_INTERNAL;
  }
  records = calloc(inputs->count + 1U, sizeof(*records));
  paths = calloc(inputs->count + 1U, sizeof(*paths));
  hashes = calloc(inputs->count + 1U, sizeof(*hashes));
  roles = calloc(inputs->count + 1U, sizeof(*roles));
  if (records == 0 || paths == 0 || hashes == 0 || roles == 0) {
    free(path_copy); free(role_copy); free(hash_copy);
    free(records); free(paths); free(hashes); free(roles);
    return CONFIT_ERR_INTERNAL;
  }
  if (inputs->count > 0U) {
    memcpy(records, inputs->records, inputs->count * sizeof(*records));
    memcpy(paths, inputs->paths, inputs->count * sizeof(*paths));
    memcpy(hashes, inputs->hashes, inputs->count * sizeof(*hashes));
    memcpy(roles, inputs->roles, inputs->count * sizeof(*roles));
  }
  free(inputs->records);
  free(inputs->paths);
  free(inputs->hashes);
  free(inputs->roles);
  inputs->records = records;
  inputs->paths = paths;
  inputs->hashes = hashes;
  inputs->roles = roles;
  inputs->paths[inputs->count] = path_copy;
  inputs->hashes[inputs->count] = hash_copy;
  inputs->roles[inputs->count] = role_copy;
  inputs->records[inputs->count].path = path_copy;
  inputs->records[inputs->count].content_hash = hash_copy;
  inputs->records[inputs->count].role = role_copy;
  inputs->count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_cli_v3_input_set_add_file(
    ConfitCliV3InputSet *inputs, const char *logical_path, const char *physical_path,
    const char *role, ConfitDiagnostic *diagnostic) {
  char *text = 0;
  ConfitStatus status = confit_host_read_text_file(physical_path, &text, 0U,
                                                    diagnostic);
  if (status == CONFIT_OK) {
    status = confit_cli_v3_input_set_add_text(inputs, logical_path, role, text,
                                               diagnostic);
  }
  confit_host_free(text);
  return status;
}

static ConfitStatus confit_cli_v3_logical_path(const ConfitV2Project *project,
                                                const char *physical_path,
                                                char *out, size_t out_size,
                                                ConfitDiagnostic *diagnostic) {
  const char *root = project->config_root;
  const size_t root_size = strlen(root);
  const char *relative;
  if (strncmp(root, physical_path, root_size) != 0 ||
      (physical_path[root_size] != '/' && physical_path[root_size] != '\\')) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, physical_path, 0U, 0U,
                          "semantic input escapes the configuration root");
    return CONFIT_ERR_SCHEMA;
  }
  relative = physical_path + root_size + 1U;
  if (strlen(relative) + 1U > out_size) return CONFIT_ERR_INTERNAL;
  memcpy(out, relative, strlen(relative) + 1U);
  return CONFIT_OK;
}

static ConfitStatus confit_cli_v3_input_set_add_project_file(
    ConfitCliV3InputSet *inputs, const ConfitV2Project *project,
    const char *physical_path, const char *role, ConfitDiagnostic *diagnostic) {
  char logical_path[4096];
  ConfitStatus status;

  if (physical_path == 0 || physical_path[0] == '\0') return CONFIT_OK;
  status = confit_cli_v3_logical_path(project, physical_path, logical_path,
                                      sizeof(logical_path), diagnostic);
  if (status == CONFIT_OK) status = confit_cli_v3_input_set_add_file(
      inputs, logical_path, physical_path, role, diagnostic);
  return status;
}

static ConfitStatus confit_cli_v3_input_set_add_directory(
    ConfitCliV3InputSet *inputs, const ConfitV2Project *project,
    const ConfitV2StringList *directories, const char *role,
    ConfitDiagnostic *diagnostic) {
  size_t directory_index;
  ConfitStatus status = CONFIT_OK;

  for (directory_index = 0U; status == CONFIT_OK &&
                            directory_index < directories->count;
       ++directory_index) {
    char directory[4096];
    char **paths = 0;
    size_t path_count = 0U;
    size_t path_index;
    status = confit_host_path_join(directory, sizeof(directory),
                                   project->config_root,
                                   directories->items[directory_index], diagnostic);
    if (status == CONFIT_OK) status = confit_host_list_toml_files(
        directory, &paths, &path_count, diagnostic);
    for (path_index = 0U; status == CONFIT_OK && path_index < path_count;
         ++path_index) {
      status = confit_cli_v3_input_set_add_project_file(inputs, project,
                                                         paths[path_index], role,
                                                         diagnostic);
    }
    confit_host_string_list_free(paths, path_count);
  }
  return status;
}

static ConfitStatus confit_cli_v3_collect_inputs(
    const ConfitCliV2Args *args, const ConfitCliV2Context *context,
    const ConfitComponentCatalog *catalog,
    ConfitCliV3InputSet *out_inputs, ConfitDiagnostic *diagnostic) {
  const ConfitV2Project *project = context->project;
  ConfitStatus status;
  size_t index;

  memset(out_inputs, 0, sizeof(*out_inputs));
  status = confit_cli_v3_input_set_add_project_file(out_inputs, project,
                                                     project->span.path,
                                                     "project", diagnostic);
  for (index = 0U; status == CONFIT_OK && index < project->import_count; ++index) {
    status = confit_cli_v3_input_set_add_project_file(
        out_inputs, project, project->imports[index].canonical_path, "schema",
        diagnostic);
  }
  for (index = 0U; status == CONFIT_OK && index < project->symbol_count; ++index) {
    status = confit_cli_v3_input_set_add_project_file(
        out_inputs, project, project->symbols[index].span.path, "schema", diagnostic);
  }
  for (index = 0U; status == CONFIT_OK && index < project->menu_count; ++index) {
    status = confit_cli_v3_input_set_add_project_file(
        out_inputs, project, project->menus[index].span.path, "schema", diagnostic);
  }
  for (index = 0U; status == CONFIT_OK && index < project->choice_count; ++index) {
    status = confit_cli_v3_input_set_add_project_file(
        out_inputs, project, project->choices[index].span.path, "schema", diagnostic);
  }
  for (index = 0U; status == CONFIT_OK && index < project->constraint_count; ++index) {
    status = confit_cli_v3_input_set_add_project_file(
        out_inputs, project, project->constraints[index].span.path, "schema", diagnostic);
  }
  if (status == CONFIT_OK) status = confit_cli_v3_input_set_add_directory(
      out_inputs, project, &project->profile_dirs, "profile", diagnostic);
  if (status == CONFIT_OK) status = confit_cli_v3_input_set_add_directory(
      out_inputs, project, &project->target_dirs, "target", diagnostic);
  if (status == CONFIT_OK) status = confit_cli_v3_input_set_add_directory(
      out_inputs, project, &project->selection_dirs, "selection", diagnostic);
  for (index = 0U; status == CONFIT_OK && catalog != 0 &&
                         index < catalog->component_count; ++index) {
    char physical_path[4096];
    status = confit_host_path_join(physical_path, sizeof(physical_path),
                                   catalog->project_root,
                                   catalog->components[index].manifest_path,
                                   diagnostic);
    if (status == CONFIT_OK) status = confit_cli_v3_input_set_add_file(
        out_inputs, catalog->components[index].manifest_path, physical_path,
        "component-manifest", diagnostic);
  }
  for (index = 0U; status == CONFIT_OK && index < args->set_count; ++index) {
    char logical_path[64];
    const int written = snprintf(logical_path, sizeof(logical_path),
                                 "cli/override/%04llu", (unsigned long long)index);
    if (written < 0 || (size_t)written >= sizeof(logical_path)) {
      status = CONFIT_ERR_INTERNAL;
    } else {
      status = confit_cli_v3_input_set_add_text(out_inputs, logical_path,
                                                 "override", args->sets[index],
                                                 diagnostic);
    }
  }
  if (status != CONFIT_OK) confit_cli_v3_input_set_clear(out_inputs);
  return status;
}

static ConfitStatus confit_cli_v2_parse(const char *command, int argc,
                                         char **argv, ConfitCliV2Args *args,
                                         ConfitDiagnostic *diagnostic) {
  int index;
  int first_index = 2;

  confit_cli_v2_args_init(args);
  if (strcmp(command, "component") == 0) {
    if (argc < 3 || (strcmp(argv[2], "check") != 0 &&
                     strcmp(argv[2], "list") != 0 &&
                     strcmp(argv[2], "explain") != 0 &&
                     strcmp(argv[2], "why") != 0 &&
                     strcmp(argv[2], "deps") != 0 &&
                     strcmp(argv[2], "rdeps") != 0 &&
                     strcmp(argv[2], "providers") != 0)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                            "component requires check, list, explain, why, deps, rdeps, or providers");
      return CONFIT_ERR_INVALID_ARGUMENT;
    }
    args->component_action = argv[2];
    first_index = 3;
  }
  for (index = first_index; index < argc; ++index) {
    const char *arg = argv[index];
    const char *value = 0;

    if (strcmp(arg, "--strict") == 0) {
      args->strict = 1;
      continue;
    }
    if (strcmp(arg, "--force") == 0) {
      args->force = 1;
      continue;
    }
    if (strcmp(arg, "--dry-run") == 0) {
      args->dry_run = 1;
      continue;
    }
    if (strcmp(arg, "--show-hidden") == 0) {
      continue;
    }
    if (index + 1 < argc &&
        (strcmp(arg, "--project") == 0 || strcmp(arg, "--profile") == 0 ||
         strcmp(arg, "--target") == 0 || strcmp(arg, "--out") == 0 ||
         strcmp(arg, "--format") == 0 || strcmp(arg, "--artifact") == 0 ||
         strcmp(arg, "--set") == 0 || strcmp(arg, "--base") == 0 ||
         strcmp(arg, "--kind") == 0 || strcmp(arg, "--category") == 0 ||
         strcmp(arg, "--tag") == 0 || strcmp(arg, "--query") == 0 ||
         strcmp(arg, "--diagnostic-format") == 0)) {
      value = argv[++index];
      if (strcmp(arg, "--project") == 0) args->project = value;
      else if (strcmp(arg, "--profile") == 0) args->profile = value;
      else if (strcmp(arg, "--target") == 0) args->target = value;
      else if (strcmp(arg, "--out") == 0) args->out = value;
      else if (strcmp(arg, "--format") == 0) args->format = value;
      else if (strcmp(arg, "--artifact") == 0) args->artifact = value;
      else if (strcmp(arg, "--base") == 0) args->base = value;
      else if (strcmp(arg, "--kind") == 0) args->kind = value;
      else if (strcmp(arg, "--category") == 0) args->category = value;
      else if (strcmp(arg, "--tag") == 0) args->tag = value;
      else if (strcmp(arg, "--query") == 0) args->query = value;
      else if (strcmp(arg, "--diagnostic-format") == 0) {
        if (strcmp(value, "json") != 0) {
          confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U,
                                0U, "--diagnostic-format must be json");
          return CONFIT_ERR_INVALID_ARGUMENT;
        }
        args->diagnostic_json = 1;
      } else {
        ConfitStatus status = confit_cli_v2_add_set(args, value);
        if (status != CONFIT_OK) return status;
      }
      continue;
    }
    if (arg[0] == '-') {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                            kInvalidOption);
      return CONFIT_ERR_INVALID_ARGUMENT;
    }
    if (strcmp(command, "explain") == 0 && args->option_id == 0) {
      args->option_id = arg;
      continue;
    }
    if (strcmp(command, "component") == 0 &&
        strcmp(args->component_action, "providers") != 0 &&
        strcmp(args->component_action, "check") != 0 &&
        strcmp(args->component_action, "list") != 0 && args->component_id == 0) {
      args->component_id = arg;
      continue;
    }
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          kInvalidCommand);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_cli_v2_resolve_component_closure(
    const ConfitCliV2Context *context, const ConfitComponentCatalog *catalog,
    ConfitComponentClosure *closure, char ***out_roots, size_t *out_root_count,
    ConfitDiagnostic *diagnostic) {
  char **required_capabilities = 0;
  char **optional_capabilities = 0;
  size_t required_capability_count = 0U;
  size_t optional_capability_count = 0U;
  ConfitStatus status;
  *out_roots = 0;
  *out_root_count = 0U;
  status = confit_v2_snapshot_collect_component_roots(
      context->compiled, context->snapshot, out_roots, out_root_count, diagnostic);
  if (status == CONFIT_OK) status =
      confit_v2_snapshot_collect_component_capability_requests(
          context->compiled, context->snapshot, &required_capabilities,
          &required_capability_count, &optional_capabilities,
          &optional_capability_count, diagnostic);
  if (status == CONFIT_OK) status = confit_component_catalog_resolve_selection(
      catalog, (const char *const *)*out_roots, *out_root_count,
      (const char *const *)required_capabilities, required_capability_count,
      (const char *const *)optional_capabilities, optional_capability_count,
      closure, diagnostic);
  confit_host_string_list_free(required_capabilities, required_capability_count);
  confit_host_string_list_free(optional_capabilities, optional_capability_count);
  return status;
}

static ConfitStatus confit_cli_v2_component_context_load(
    const ConfitCliV2Args *args, ConfitCliV2Context *context,
    ConfitComponentCatalog *catalog, ConfitComponentClosure *closure,
    char ***out_roots, size_t *out_root_count, ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  memset(catalog, 0, sizeof(*catalog));
  memset(closure, 0, sizeof(*closure));
  status = confit_cli_v2_context_load(args, context, diagnostic);
  if (status == CONFIT_OK) status = confit_component_catalog_load(context->project,
                                                                   catalog, diagnostic);
  if (status == CONFIT_OK) status = confit_cli_v2_resolve_component_closure(
      context, catalog, closure, out_roots, out_root_count, diagnostic);
  return status;
}

static int confit_cli_v2_private_edge_redundant_candidate(
    const ConfitComponentCatalog *catalog, const ConfitComponent *component,
    const char *dependency_id) {
  size_t index;
  for (index = 0U; index < component->component_dependency_count +
                              component->kapi_requirement_count; ++index) {
    const ConfitComponent *alternate;
    const char *root[1];
    ConfitComponentClosure closure;
    ConfitDiagnostic diagnostic;
    size_t member;
    int found = 0;
    if (index < component->component_dependency_count) {
      alternate = confit_component_catalog_find(
          catalog, component->component_dependencies[index]);
    } else {
      alternate = confit_component_catalog_find_kapi_provider(
          catalog, component->kapi_requires[
              index - component->component_dependency_count]);
    }
    if (alternate == 0 || strcmp(alternate->id, dependency_id) == 0) continue;
    memset(&closure, 0, sizeof(closure));
    confit_diagnostic_init(&diagnostic);
    root[0] = alternate->id;
    if (confit_component_catalog_resolve(catalog, root, 1U, &closure,
                                         &diagnostic) != CONFIT_OK) {
      confit_component_closure_clear(&closure);
      continue;
    }
    for (member = 0U; member < closure.component_count; ++member) {
      if (strcmp(closure.ordered[member]->id, dependency_id) == 0) found = 1;
    }
    confit_component_closure_clear(&closure);
    if (found) return 1;
  }
  return 0;
}

static ConfitStatus confit_cli_v2_run_component(const ConfitCliV2Args *args,
                                                 ConfitDiagnostic *diagnostic) {
  ConfitCliV2Context context;
  ConfitComponentCatalog catalog;
  ConfitComponentClosure closure;
  char **roots = 0;
  size_t root_count = 0U;
  ConfitStatus status;
  size_t index;
  const ConfitComponent *requested = 0;
  memset(&context, 0, sizeof(context));
  status = confit_cli_v2_component_context_load(args, &context, &catalog, &closure,
                                                 &roots, &root_count, diagnostic);
  if (status == CONFIT_OK && args->component_id == 0 &&
      strcmp(args->component_action, "check") != 0 &&
      strcmp(args->component_action, "list") != 0 &&
      strcmp(args->component_action, "providers") != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          "component action requires an exact component ID");
    status = CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (status == CONFIT_OK && args->component_id != 0) {
    requested = confit_component_catalog_find(&catalog, args->component_id);
    if (requested == 0) {
      static char unknown_message[1024];
      const ConfitComponent *candidates[5];
      size_t candidate_count = confit_component_catalog_suggest(
          &catalog, args->component_id, candidates, 5U);
      size_t used = (size_t)snprintf(unknown_message, sizeof(unknown_message),
                                    "unknown component ID");
      for (index = 0U; index < candidate_count && used < sizeof(unknown_message); ++index) {
        int written = snprintf(unknown_message + used, sizeof(unknown_message) - used,
                               "%s%s", index == 0U ? "; candidates: " : ", ",
                               candidates[index]->id);
        if (written < 0 || (size_t)written >= sizeof(unknown_message) - used) break;
        used += (size_t)written;
      }
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, args->component_id,
                            0U, 0U, unknown_message);
      status = CONFIT_ERR_INVALID_ARGUMENT;
    }
  }
  if (status == CONFIT_OK && strcmp(args->component_action, "check") == 0) {
    status = confit_host_stdout_write_line("component check ok");
  } else if (status == CONFIT_OK && strcmp(args->component_action, "list") == 0) {
    for (index = 0U; status == CONFIT_OK && index < catalog.component_count; ++index) {
      status = confit_host_stdout_write(catalog.components[index].id);
      if (status == CONFIT_OK) status = confit_host_stdout_write("\t");
      if (status == CONFIT_OK) status = confit_host_stdout_write_line(
          confit_component_kind_name(catalog.components[index].kind));
    }
  } else if (status == CONFIT_OK && strcmp(args->component_action, "explain") == 0) {
    if (args->component_id == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                            "component explain requires a component ID");
      status = CONFIT_ERR_INVALID_ARGUMENT;
    } else {
      int selected = 0;
      for (index = 0U; index < closure.component_count; ++index) {
        if (closure.ordered[index] == requested) selected = 1;
      }
      {
        char line[1024];
        const int written = snprintf(line, sizeof(line),
            "%s\nkind: %s\nmanifest: %s\nmakefile: %s\nselected: %s\n",
            requested->id, confit_component_kind_name(requested->kind),
            requested->manifest_path, requested->makefile_path,
            selected ? "true" : "false");
        if (written < 0 || (size_t)written >= sizeof(line)) status = CONFIT_ERR_INTERNAL;
        else status = confit_host_stdout_write(line);
      }
    }
  } else if (status == CONFIT_OK && strcmp(args->component_action, "why") == 0) {
    int found = 0;
    for (index = 0U; status == CONFIT_OK && index < closure.reason_count; ++index) {
      const ConfitComponentReason *reason = &closure.reasons[index];
      char line[1024];
      int written;
      if (strcmp(reason->component_id, requested->id) != 0) continue;
      found = 1;
      written = snprintf(line, sizeof(line), "%s\t%s\tfrom=%s\trequirement=%s\tsource=%s:%llu:%llu\n",
          reason->component_id, confit_component_reason_kind_name(reason->kind),
          reason->from_id != 0 ? reason->from_id : "-", reason->requirement,
          reason->source_path, (unsigned long long)reason->source_line,
          (unsigned long long)reason->source_column);
      status = written < 0 || (size_t)written >= sizeof(line)
                   ? CONFIT_ERR_INTERNAL : confit_host_stdout_write(line);
    }
    if (status == CONFIT_OK && !found) status = confit_host_stdout_write_line("not-selected");
  } else if (status == CONFIT_OK && strcmp(args->component_action, "deps") == 0) {
    ConfitComponentClosure dependency_closure;
    const char *one_root[1];
    memset(&dependency_closure, 0, sizeof(dependency_closure));
    one_root[0] = requested->id;
    status = confit_component_catalog_resolve(&catalog, one_root, 1U,
                                               &dependency_closure, diagnostic);
    for (index = 0U; status == CONFIT_OK &&
                    index < dependency_closure.component_count; ++index) {
      const ConfitComponent *dependency = dependency_closure.ordered[index];
      int direct = 0;
      int private_direct = 0;
      int redundant_candidate = 0;
      size_t edge;
      char line[1024];
      int written;
      if (dependency == requested) continue;
      for (edge = 0U; edge < dependency_closure.reason_count; ++edge) {
        const ConfitComponentReason *reason = &dependency_closure.reasons[edge];
        if (strcmp(reason->component_id, dependency->id) == 0 &&
            reason->from_id != 0 && strcmp(reason->from_id, requested->id) == 0) {
          direct = 1;
          if (reason->kind == CONFIT_COMPONENT_REASON_PRIVATE_DEPENDENCY) private_direct = 1;
        }
      }
      if (private_direct) redundant_candidate =
          confit_cli_v2_private_edge_redundant_candidate(
              &catalog, requested, dependency->id);
      written = snprintf(line, sizeof(line), "%s\t%s%s\n", dependency->id,
                         direct ? "direct" : "transitive",
                         redundant_candidate ? "\tredundant-candidate" : "");
      status = written < 0 || (size_t)written >= sizeof(line)
                   ? CONFIT_ERR_INTERNAL : confit_host_stdout_write(line);
    }
    confit_component_closure_clear(&dependency_closure);
  } else if (status == CONFIT_OK && strcmp(args->component_action, "rdeps") == 0) {
    for (index = 0U; status == CONFIT_OK && index < catalog.component_count; ++index) {
      ConfitComponentClosure candidate_closure;
      const char *one_root[1];
      size_t member;
      int reaches = 0;
      int direct = 0;
      size_t edge;
      if (&catalog.components[index] == requested) continue;
      memset(&candidate_closure, 0, sizeof(candidate_closure));
      one_root[0] = catalog.components[index].id;
      status = confit_component_catalog_resolve(&catalog, one_root, 1U,
                                                 &candidate_closure, diagnostic);
      for (member = 0U; status == CONFIT_OK && member < candidate_closure.component_count;
           ++member) if (candidate_closure.ordered[member] == requested) reaches = 1;
      for (edge = 0U; reaches && edge < candidate_closure.reason_count; ++edge) {
        const ConfitComponentReason *reason = &candidate_closure.reasons[edge];
        if (strcmp(reason->component_id, requested->id) == 0 && reason->from_id != 0 &&
            strcmp(reason->from_id, catalog.components[index].id) == 0) direct = 1;
      }
      if (status == CONFIT_OK && reaches) {
        char line[1024];
        int written = snprintf(line, sizeof(line), "%s\t%s\n",
                               catalog.components[index].id,
                               direct ? "direct" : "transitive");
        status = written < 0 || (size_t)written >= sizeof(line)
                     ? CONFIT_ERR_INTERNAL : confit_host_stdout_write(line);
      }
      confit_component_closure_clear(&candidate_closure);
    }
  } else if (status == CONFIT_OK && strcmp(args->component_action, "providers") == 0) {
    for (index = 0U; status == CONFIT_OK && index < catalog.component_count; ++index) {
      size_t item;
      const ConfitComponent *component = &catalog.components[index];
      for (item = 0U; status == CONFIT_OK && item < component->capability_count; ++item) {
        char line[1024];
        int written = snprintf(line, sizeof(line), "capability\t%s\t%s\n",
                               component->capabilities[item], component->id);
        status = written < 0 || (size_t)written >= sizeof(line)
                     ? CONFIT_ERR_INTERNAL : confit_host_stdout_write(line);
      }
      for (item = 0U; status == CONFIT_OK && item < component->kapi_provide_count; ++item) {
        char line[1024];
        int written = snprintf(line, sizeof(line), "kapi\t%s\t%s\n",
                               component->kapi_provides[item], component->id);
        status = written < 0 || (size_t)written >= sizeof(line)
                     ? CONFIT_ERR_INTERNAL : confit_host_stdout_write(line);
      }
    }
  }
  confit_component_closure_clear(&closure);
  confit_host_string_list_free(roots, root_count);
  confit_component_catalog_clear(&catalog);
  confit_cli_v2_context_clear(&context);
  return status;
}

static int confit_cli_v2_return_error(ConfitStatus status,
                                       const ConfitDiagnostic *diagnostic,
                                       int json) {
  ConfitStatus write_status;
  ConfitCliV2Builder builder;

  if (!json) {
    write_status = confit_host_stderr_write("confit: ");
    if (write_status == CONFIT_OK && diagnostic != 0 && diagnostic->path != 0) {
      write_status = confit_host_stderr_write(diagnostic->path);
      if (write_status == CONFIT_OK) write_status = confit_host_stderr_write(": ");
    }
    if (write_status == CONFIT_OK) {
      write_status = confit_host_stderr_write_line(
          diagnostic != 0 && diagnostic->message != 0 ? diagnostic->message
                                                       : confit_status_name(status));
    }
    return confit_status_exit_code(write_status == CONFIT_OK ? status : write_status);
  }
  confit_cli_v2_builder_init(&builder);
  write_status = confit_cli_v2_builder_append(&builder,
      "{\"schema\": \"confit-diagnostic-v2\", \"status\": ");
  if (write_status == CONFIT_OK) write_status = confit_cli_v2_json_string(&builder, confit_status_name(status));
  if (write_status == CONFIT_OK) write_status = confit_cli_v2_builder_append(&builder, ", \"message\": ");
  if (write_status == CONFIT_OK) write_status = confit_cli_v2_json_string(
      &builder, diagnostic != 0 && diagnostic->message != 0 ? diagnostic->message
                                                             : confit_status_name(status));
  if (write_status == CONFIT_OK) write_status = confit_cli_v2_builder_append(&builder, ", \"path\": ");
  if (write_status == CONFIT_OK) write_status = confit_cli_v2_json_string(
      &builder, diagnostic != 0 ? diagnostic->path : 0);
  if (write_status == CONFIT_OK) write_status = confit_cli_v2_builder_appendf(
      &builder, ", \"line\": %llu, \"column\": %llu}\n",
      (unsigned long long)(diagnostic != 0 ? diagnostic->line : 0U),
      (unsigned long long)(diagnostic != 0 ? diagnostic->column : 0U));
  if (write_status == CONFIT_OK) write_status = confit_host_stderr_write(builder.text);
  confit_cli_v2_builder_clear(&builder);
  return confit_status_exit_code(write_status == CONFIT_OK ? status : write_status);
}

static void confit_cli_v2_context_clear(ConfitCliV2Context *context) {
  size_t index;

  for (index = 0U; index < context->override_count; ++index) {
    free(context->override_ids[index]);
  }
  free(context->override_ids);
  free(context->overrides);
  confit_v2_snapshot_free(context->snapshot);
  confit_v2_compiled_structure_free(context->compiled);
  confit_v2_linked_project_free(context->linked);
  confit_v2_project_free(context->project);
  memset(context, 0, sizeof(*context));
}

static ConfitStatus confit_cli_v2_make_overrides(
    const ConfitCliV2Args *args, ConfitCliV2Context *context,
    ConfitDiagnostic *diagnostic) {
  size_t index;

  if (args->set_count == 0U) return CONFIT_OK;
  context->overrides = (ConfitV2UserOverride *)calloc(
      args->set_count, sizeof(*context->overrides));
  context->override_ids = (char **)calloc(args->set_count, sizeof(*context->override_ids));
  if (context->overrides == 0 || context->override_ids == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0U, 0U,
                          "failed to allocate schema v2 CLI overrides");
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < args->set_count; ++index) {
    const char *equals = strchr(args->sets[index], '=');
    size_t size;
    if (equals == 0 || equals == args->sets[index]) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
                            args->sets[index], 0U, 0U,
                            "schema v2 --set requires option.id=value");
      return CONFIT_ERR_INVALID_ARGUMENT;
    }
    size = (size_t)(equals - args->sets[index]);
    context->override_ids[index] = (char *)malloc(size + 1U);
    if (context->override_ids[index] == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0U, 0U,
                            "failed to allocate schema v2 override id");
      return CONFIT_ERR_INTERNAL;
    }
    memcpy(context->override_ids[index], args->sets[index], size);
    context->override_ids[index][size] = '\0';
    context->overrides[index].option_id = context->override_ids[index];
    context->overrides[index].value_text = equals + 1U;
  }
  context->override_count = args->set_count;
  return CONFIT_OK;
}

static ConfitStatus confit_cli_v2_context_load(const ConfitCliV2Args *args,
                                                ConfitCliV2Context *context,
                                                ConfitDiagnostic *diagnostic) {
  ConfitV2LedgerOptions options;
  ConfitStatus status;

  memset(context, 0, sizeof(*context));
  if (args->project == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          kMissingProject);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  status = confit_v2_schema_load_project(args->project, &context->project,
                                          diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_schema_link_project(context->project, &context->linked,
                                            diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_compile_structure(context->linked, &context->compiled,
                                         diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_cli_v2_make_overrides(args, context, diagnostic);
  }
  if (status == CONFIT_OK) {
    memset(&options, 0, sizeof(options));
    options.profile_name = args->profile;
    options.target_name = args->target;
    options.user_overrides = context->overrides;
    options.user_override_count = context->override_count;
    status = confit_v2_snapshot_resolve(context->compiled, &options,
                                        &context->snapshot, diagnostic);
  }
  if (status != CONFIT_OK) confit_cli_v2_context_clear(context);
  return status;
}

static ConfitStatus confit_cli_v2_render_resolve(
    const ConfitV2Snapshot *snapshot, const char *format, char **out_text,
    ConfitDiagnostic *diagnostic) {
  ConfitCliV2Builder builder;
  size_t index;
  ConfitStatus status;

  *out_text = 0;
  confit_cli_v2_builder_init(&builder);
  if (strcmp(format, "json") == 0) {
    status = confit_cli_v2_builder_append(&builder,
        "{\n  \"schema\": \"confit-resolved-v2\",\n  \"values\": [");
    for (index = 0U; status == CONFIT_OK &&
                    index < confit_v2_snapshot_option_count(snapshot); ++index) {
      const ConfitV2SnapshotOption *option =
          confit_v2_snapshot_option_at(snapshot, index);
      status = confit_cli_v2_builder_append(&builder, index == 0U ? "\n    {\"id\": " : ",\n    {\"id\": ");
      if (status == CONFIT_OK) status = confit_cli_v2_json_string(&builder, option->id);
      if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, ", \"type\": ");
      if (status == CONFIT_OK) status = confit_cli_v2_json_string(&builder, confit_cli_v2_type_name(option->type));
      if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, ", \"available\": ");
      if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, option->available ? "true" : "false");
      if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, ", \"visible\": ");
      if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, option->visible ? "true" : "false");
      if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, ", \"effective\": ");
      if (status == CONFIT_OK) status = confit_cli_v2_append_value(&builder, &option->effective_value, 1);
      if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, ", \"effective_origin\": ");
      if (status == CONFIT_OK) status = confit_cli_v2_json_string(&builder, confit_cli_v2_effective_origin_name(option->effective_origin));
      if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, "}");
    }
    if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, "\n  ]\n}\n");
  } else if (strcmp(format, "toml") == 0) {
    status = confit_cli_v2_builder_append(&builder, "schema_version = 2\n[values]\n");
    for (index = 0U; status == CONFIT_OK &&
                    index < confit_v2_snapshot_option_count(snapshot); ++index) {
      const ConfitV2SnapshotOption *option = confit_v2_snapshot_option_at(snapshot, index);
      status = confit_cli_v2_toml_string(&builder, option->id);
      if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, " = ");
      if (status == CONFIT_OK) status = confit_cli_v2_append_value(&builder, &option->effective_value, 1);
      if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, "\n");
    }
  } else if (strcmp(format, "text") == 0) {
    status = CONFIT_OK;
    for (index = 0U; status == CONFIT_OK &&
                    index < confit_v2_snapshot_option_count(snapshot); ++index) {
      const ConfitV2SnapshotOption *option = confit_v2_snapshot_option_at(snapshot, index);
      status = confit_cli_v2_builder_append(&builder, option->id);
      if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, " = ");
      if (status == CONFIT_OK) status = confit_cli_v2_append_value(&builder, &option->effective_value, 0);
      if (status == CONFIT_OK) status = confit_cli_v2_builder_appendf(
          &builder, "\n  type: %s\n  requested: %s\n  effective: %s\n  source: %s:%llu\n",
          confit_cli_v2_type_name(option->type),
          option->requested.is_present ? "present" : "none",
          confit_cli_v2_effective_origin_name(option->effective_origin),
          option->effective_source_path != 0 ? option->effective_source_path : "",
          (unsigned long long)option->effective_source_line);
    }
  } else {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, format, 0U, 0U,
                          "schema v2 resolve --format must be text, json, or toml");
    status = CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (status == CONFIT_OK) {
    *out_text = confit_cli_v2_builder_take(&builder);
    if (*out_text == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_cli_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_cli_v2_run_check(const ConfitCliV2Args *args,
                                             ConfitDiagnostic *diagnostic) {
  ConfitCliV2Context context;
  ConfitComponentCatalog catalog;
  ConfitComponentClosure closure;
  char **roots = 0;
  size_t root_count = 0U;
  ConfitStatus status;
  memset(&catalog, 0, sizeof(catalog));
  memset(&closure, 0, sizeof(closure));
  status = confit_cli_v2_context_load(args, &context, diagnostic);
  if (status == CONFIT_OK) status = confit_component_catalog_load(context.project,
                                                                   &catalog, diagnostic);
  if (status == CONFIT_OK) status = confit_cli_v2_resolve_component_closure(
      &context, &catalog, &closure, &roots, &root_count, diagnostic);
  if (status == CONFIT_OK) status = confit_host_stdout_write_line("check ok");
  confit_component_closure_clear(&closure);
  confit_host_string_list_free(roots, root_count);
  confit_component_catalog_clear(&catalog);
  confit_cli_v2_context_clear(&context);
  return status;
}

static ConfitStatus confit_cli_v2_run_resolve(const ConfitCliV2Args *args,
                                               ConfitDiagnostic *diagnostic) {
  ConfitCliV2Context context;
  char *text = 0;
  ConfitStatus status = confit_cli_v2_context_load(args, &context, diagnostic);

  if (status == CONFIT_OK) status = confit_cli_v2_render_resolve(context.snapshot,
                                                                   args->format,
                                                                   &text, diagnostic);
  if (status == CONFIT_OK) status = confit_host_stdout_write(text);
  free(text);
  confit_cli_v2_context_clear(&context);
  return status;
}

static ConfitStatus confit_cli_v2_run_gen(const ConfitCliV2Args *args,
                                           ConfitDiagnostic *diagnostic) {
  ConfitCliV2Context context;
  ConfitCliV3InputSet inputs;
  ConfitV3ArtifactOptions artifact_options;
  ConfitV3ArtifactSet artifacts;
  ConfitV3PublishOptions publish_options;
  ConfitComponentCatalog catalog;
  ConfitComponentClosure closure;
  char **roots = 0;
  size_t root_count = 0U;
  size_t changed = 0U;
  ConfitStatus status;

  if (args->out == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          "schema v2 gen requires --out");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (strcmp(args->artifact, "bundle") != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, args->artifact,
                          0U, 0U,
                          "schema v2 gen accepts only `bundle`; partial and legacy artifact selectors are unsupported");
    return CONFIT_ERR_UNSUPPORTED;
  }
  status = confit_cli_v2_context_load(args, &context, diagnostic);
  memset(&artifacts, 0, sizeof(artifacts));
  memset(&inputs, 0, sizeof(inputs));
  memset(&artifact_options, 0, sizeof(artifact_options));
  memset(&publish_options, 0, sizeof(publish_options));
  memset(&catalog, 0, sizeof(catalog));
  memset(&closure, 0, sizeof(closure));
  if (status == CONFIT_OK) {
    status = confit_component_catalog_load(context.project, &catalog, diagnostic);
  }
  if (status == CONFIT_OK) status = confit_cli_v2_resolve_component_closure(
      &context, &catalog, &closure, &roots, &root_count, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_cli_v3_collect_inputs(args, &context, &catalog, &inputs,
                                          diagnostic);
  }
  if (status == CONFIT_OK) {
    artifact_options.inputs = inputs.records;
    artifact_options.input_count = inputs.count;
    artifact_options.tool_identity = "confit-" CONFIT_VERSION_RELEASE;
    artifact_options.component_catalog = &catalog;
    artifact_options.component_closure = &closure;
    status = confit_v3_generate_artifacts(context.snapshot, &artifact_options,
                                           &artifacts, diagnostic);
  }
  if (status == CONFIT_OK && args->dry_run) {
    status = confit_host_stdout_write_line("gen dry-run ok");
  } else if (status == CONFIT_OK) {
    publish_options.output_root = args->out;
    status = confit_v3_publish_artifacts(&publish_options, &artifacts, &changed,
                                          diagnostic);
  }
  if (status == CONFIT_OK && !args->dry_run) {
    char line[192];
    (void)snprintf(line, sizeof(line), "gen ok: bundle=%s changed=%llu",
                   artifacts.bundle_digest, (unsigned long long)changed);
    status = confit_host_stdout_write_line(line);
  }
  confit_v3_artifact_set_clear(&artifacts);
  confit_cli_v3_input_set_clear(&inputs);
  confit_component_closure_clear(&closure);
  confit_host_string_list_free(roots, root_count);
  confit_component_catalog_clear(&catalog);
  confit_cli_v2_context_clear(&context);
  return status;
}

static ConfitStatus confit_cli_v2_run_explain(const ConfitCliV2Args *args,
                                               ConfitDiagnostic *diagnostic) {
  ConfitCliV2Context context;
  const ConfitV2SnapshotOption *option;
  ConfitCliV2Builder builder;
  ConfitStatus status;

  if (args->option_id == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          "schema v2 explain requires an option id");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  status = confit_cli_v2_context_load(args, &context, diagnostic);
  option = status == CONFIT_OK
               ? confit_v2_snapshot_find_option(context.snapshot, args->option_id)
               : 0;
  if (status == CONFIT_OK && option == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, args->option_id,
                          0U, 0U, "unknown schema v2 option");
    status = CONFIT_ERR_INVALID_ARGUMENT;
  }
  confit_cli_v2_builder_init(&builder);
  if (status == CONFIT_OK) status = confit_cli_v2_builder_appendf(
      &builder, "%s\ntype: %s\navailable: %s\nvisible: %s\nvalue: ", option->id,
      confit_cli_v2_type_name(option->type), option->available ? "true" : "false",
      option->visible ? "true" : "false");
  if (status == CONFIT_OK) status = confit_cli_v2_append_value(&builder, &option->effective_value, 0);
  if (status == CONFIT_OK) status = confit_cli_v2_builder_appendf(
      &builder, "\neffective origin: %s\nsource: %s:%llu:%llu\nrequested: %s\n",
      confit_cli_v2_effective_origin_name(option->effective_origin),
      option->effective_source_path != 0 ? option->effective_source_path : "",
      (unsigned long long)option->effective_source_line,
      (unsigned long long)option->effective_source_column,
      option->requested.is_present ? "present" : "none");
  if (status == CONFIT_OK) status = confit_host_stdout_write(builder.text);
  confit_cli_v2_builder_clear(&builder);
  confit_cli_v2_context_clear(&context);
  return status;
}

static const char *confit_cli_v2_graph_kind_name(ConfitV2CompiledGraphKind kind) {
  switch (kind) {
  case CONFIT_V2_COMPILED_GRAPH_EVALUATION: return "evaluation";
  case CONFIT_V2_COMPILED_GRAPH_VISIBILITY: return "visibility";
  case CONFIT_V2_COMPILED_GRAPH_CHOICE: return "choice";
  case CONFIT_V2_COMPILED_GRAPH_CONSTRAINT: return "constraint";
  default: return "unknown";
  }
}

static ConfitStatus confit_cli_v2_render_graph(const ConfitV2CompiledStructure *compiled,
                                                const char *format, char **out_text,
                                                ConfitDiagnostic *diagnostic) {
  ConfitCliV2Builder builder;
  ConfitV2CompiledGraphKind kind;
  int first = 1;
  ConfitStatus status;

  *out_text = 0;
  confit_cli_v2_builder_init(&builder);
  if (strcmp(format, "json") == 0) {
    status = confit_cli_v2_builder_append(&builder,
        "{\n  \"schema\": \"confit-graph-v2\",\n  \"edges\": [");
    for (kind = CONFIT_V2_COMPILED_GRAPH_EVALUATION; status == CONFIT_OK &&
         kind <= CONFIT_V2_COMPILED_GRAPH_CONSTRAINT; ++kind) {
      const ConfitV2CompiledGraph *graph = confit_v2_compiled_structure_graph(compiled, kind);
      size_t index;
      for (index = 0U; graph != 0 && status == CONFIT_OK && index < graph->edge_count; ++index) {
        const ConfitV2CompiledGraphEdge *edge = &graph->edges[index];
        status = confit_cli_v2_builder_append(&builder, first ? "\n    {\"owner\": " : ",\n    {\"owner\": ");
        first = 0;
        if (status == CONFIT_OK) status = confit_cli_v2_json_string(&builder, edge->owner_id);
        if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, ", \"target\": ");
        if (status == CONFIT_OK) status = confit_cli_v2_json_string(&builder, edge->target->id);
        if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, ", \"kind\": ");
        if (status == CONFIT_OK) status = confit_cli_v2_json_string(&builder, confit_cli_v2_graph_kind_name(kind));
        if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, "}");
      }
    }
    if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, "\n  ]\n}\n");
  } else if (strcmp(format, "dot") == 0) {
    status = confit_cli_v2_builder_append(&builder, "digraph confit_v2 {\n");
    for (kind = CONFIT_V2_COMPILED_GRAPH_EVALUATION; status == CONFIT_OK &&
         kind <= CONFIT_V2_COMPILED_GRAPH_CONSTRAINT; ++kind) {
      const ConfitV2CompiledGraph *graph = confit_v2_compiled_structure_graph(compiled, kind);
      size_t index;
      for (index = 0U; graph != 0 && status == CONFIT_OK && index < graph->edge_count; ++index) {
        status = confit_cli_v2_builder_appendf(&builder, "  \"%s\" -> \"%s\" [label=\"%s\"];\n",
                                               graph->edges[index].owner_id,
                                               graph->edges[index].target->id,
                                               confit_cli_v2_graph_kind_name(kind));
      }
    }
    if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, "}\n");
  } else {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, format, 0U, 0U,
                          "schema v2 graph --format must be json or dot");
    status = CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (status == CONFIT_OK) {
    *out_text = confit_cli_v2_builder_take(&builder);
    if (*out_text == 0) status = CONFIT_ERR_INTERNAL;
  }
  confit_cli_v2_builder_clear(&builder);
  return status;
}

static ConfitStatus confit_cli_v2_run_graph(const ConfitCliV2Args *args,
                                             ConfitDiagnostic *diagnostic) {
  ConfitCliV2Context context;
  char *text = 0;
  ConfitStatus status = confit_cli_v2_context_load(args, &context, diagnostic);
  if (status == CONFIT_OK) status = confit_cli_v2_render_graph(context.compiled,
                                                                 args->format,
                                                                 &text, diagnostic);
  if (status == CONFIT_OK) status = confit_host_stdout_write(text);
  free(text);
  confit_cli_v2_context_clear(&context);
  return status;
}

static int confit_cli_v2_text_matches(const char *text, const char *query) {
  return query == 0 || query[0] == '\0' || (text != 0 && strstr(text, query) != 0);
}

static int confit_cli_v2_symbol_has_tag(const ConfitV2Symbol *symbol,
                                         const char *tag) {
  size_t index;
  if (tag == 0) return 1;
  for (index = 0U; index < symbol->tags.count; ++index) {
    if (strcmp(symbol->tags.items[index], tag) == 0) return 1;
  }
  return 0;
}

static ConfitStatus confit_cli_v2_run_list(const ConfitCliV2Args *args,
                                            ConfitDiagnostic *diagnostic) {
  ConfitV2Project *project = 0;
  ConfitStatus status;
  size_t index;

  if (args->project == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          kMissingProject);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  status = confit_v2_schema_load_project(args->project, &project, diagnostic);
  if (status != CONFIT_OK) return status;
  if (strcmp(args->kind, "options") == 0) {
    for (index = 0U; status == CONFIT_OK && index < project->symbol_count; ++index) {
      const ConfitV2Symbol *symbol = &project->symbols[index];
      if ((args->category != 0 && (symbol->menu == 0 || strcmp(symbol->menu, args->category) != 0)) ||
          !confit_cli_v2_symbol_has_tag(symbol, args->tag) ||
          (!confit_cli_v2_text_matches(symbol->id, args->query) &&
           !confit_cli_v2_text_matches(symbol->prompt, args->query) &&
           !confit_cli_v2_text_matches(symbol->help, args->query))) continue;
      status = confit_host_stdout_write(symbol->id);
      if (status == CONFIT_OK) status = confit_host_stdout_write("\t");
      if (status == CONFIT_OK) status = confit_host_stdout_write_line(confit_cli_v2_type_name(symbol->type));
    }
  } else if (strcmp(args->kind, "categories") == 0) {
    for (index = 0U; status == CONFIT_OK && index < project->menu_count; ++index) {
      status = confit_host_stdout_write_line(project->menus[index].id);
    }
  } else if (strcmp(args->kind, "tags") == 0) {
    for (index = 0U; status == CONFIT_OK && index < project->symbol_count; ++index) {
      size_t tag_index;
      for (tag_index = 0U; status == CONFIT_OK && tag_index < project->symbols[index].tags.count;
           ++tag_index) status = confit_host_stdout_write_line(project->symbols[index].tags.items[tag_index]);
    }
  } else if (strcmp(args->kind, "profiles") == 0 || strcmp(args->kind, "targets") == 0) {
    const ConfitV2StringList *directories = strcmp(args->kind, "profiles") == 0
                                                 ? &project->profile_dirs : &project->target_dirs;
    for (index = 0U; status == CONFIT_OK && index < directories->count; ++index) {
      status = confit_host_stdout_write_line(directories->items[index]);
    }
  } else {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, args->kind, 0U,
                          0U, "schema v2 list --kind is invalid");
    status = CONFIT_ERR_INVALID_ARGUMENT;
  }
  confit_v2_project_free(project);
  return status;
}

static int confit_cli_v2_value_equal(const ConfitV2Value *left,
                                     const ConfitV2Value *right) {
  size_t index;
  if (left->kind != right->kind) return 0;
  switch (left->kind) {
  case CONFIT_V2_VALUE_BOOL: return left->as.bool_value == right->as.bool_value;
  case CONFIT_V2_VALUE_TRISTATE: return left->as.tristate_value == right->as.tristate_value;
  case CONFIT_V2_VALUE_INT: return left->as.int_value == right->as.int_value;
  case CONFIT_V2_VALUE_UINT: return left->as.uint_value == right->as.uint_value;
  case CONFIT_V2_VALUE_FLOAT: return left->as.float_value == right->as.float_value;
  case CONFIT_V2_VALUE_STRING: return strcmp(left->as.string_value, right->as.string_value) == 0;
  case CONFIT_V2_VALUE_STRING_LIST:
    if (left->as.string_list.count != right->as.string_list.count) return 0;
    for (index = 0U; index < left->as.string_list.count; ++index) {
      if (strcmp(left->as.string_list.items[index], right->as.string_list.items[index]) != 0) return 0;
    }
    return 1;
  case CONFIT_V2_VALUE_UNSET: return 1;
  default: return 0;
  }
}

static ConfitStatus confit_cli_v2_run_diff(const ConfitCliV2Args *args,
                                            ConfitDiagnostic *diagnostic) {
  ConfitCliV2Args base_args = *args;
  ConfitCliV2Context base;
  ConfitCliV2Context current;
  ConfitCliV2Builder builder;
  size_t index;
  size_t changes = 0U;
  ConfitStatus status;

  memset(&base, 0, sizeof(base));
  memset(&current, 0, sizeof(current));

  if (args->base == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          "schema v2 diff requires --base");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  base_args.profile = args->base;
  status = confit_cli_v2_context_load(&base_args, &base, diagnostic);
  if (status == CONFIT_OK) status = confit_cli_v2_context_load(args, &current, diagnostic);
  confit_cli_v2_builder_init(&builder);
  if (status == CONFIT_OK && strcmp(args->format, "json") == 0) {
    status = confit_cli_v2_builder_append(&builder,
        "{\"schema\": \"confit-diff-v2\", \"changes\": [");
  } else if (status == CONFIT_OK && strcmp(args->format, "text") == 0) {
    status = confit_cli_v2_builder_appendf(&builder, "diff: %s -> %s\n", args->base,
                                           args->profile != 0 ? args->profile : "<default>");
  } else if (status == CONFIT_OK) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, args->format,
                          0U, 0U, "schema v2 diff --format must be text or json");
    status = CONFIT_ERR_INVALID_ARGUMENT;
  }
  for (index = 0U; status == CONFIT_OK &&
                  index < confit_v2_snapshot_option_count(current.snapshot); ++index) {
    const ConfitV2SnapshotOption *right = confit_v2_snapshot_option_at(current.snapshot, index);
    const ConfitV2SnapshotOption *left = confit_v2_snapshot_find_option(base.snapshot, right->id);
    if (left != 0 && left->effective_is_set == right->effective_is_set &&
        (!right->effective_is_set || confit_cli_v2_value_equal(&left->effective_value,
                                                                 &right->effective_value))) continue;
    changes += 1U;
    if (strcmp(args->format, "json") == 0) {
      status = confit_cli_v2_builder_append(&builder, changes == 1U ? "{\"id\": " : ", {\"id\": ");
      if (status == CONFIT_OK) status = confit_cli_v2_json_string(&builder, right->id);
      if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, ", \"base\": ");
      if (status == CONFIT_OK) status = confit_cli_v2_append_value(&builder,
          left != 0 ? &left->effective_value : 0, 1);
      if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, ", \"current\": ");
      if (status == CONFIT_OK) status = confit_cli_v2_append_value(&builder,
          &right->effective_value, 1);
      if (status == CONFIT_OK) status = confit_cli_v2_builder_append(&builder, "}");
    } else {
      status = confit_cli_v2_builder_appendf(&builder, "%s\n", right->id);
    }
  }
  if (status == CONFIT_OK && strcmp(args->format, "json") == 0) {
    status = confit_cli_v2_builder_appendf(&builder, "], \"changed\": %llu}\n",
                                           (unsigned long long)changes);
  } else if (status == CONFIT_OK) {
    status = confit_cli_v2_builder_appendf(&builder, "changes: %llu\n",
                                           (unsigned long long)changes);
  }
  if (status == CONFIT_OK) status = confit_host_stdout_write(builder.text);
  confit_cli_v2_builder_clear(&builder);
  confit_cli_v2_context_clear(&current);
  confit_cli_v2_context_clear(&base);
  return status;
}

int confit_cli_v2_try_run(const char *command, int argc, char **argv,
                          int *out_handled) {
  ConfitCliV2Args args;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;
  int diagnostic_json = 0;

  if (out_handled != 0) *out_handled = 0;
  if (strcmp(command, "check") != 0 && strcmp(command, "resolve") != 0 &&
      strcmp(command, "gen") != 0 && strcmp(command, "explain") != 0 &&
      strcmp(command, "list") != 0 && strcmp(command, "graph") != 0 &&
      strcmp(command, "diff") != 0 && strcmp(command, "component") != 0) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  status = confit_cli_v2_parse(command, argc, argv, &args, &diagnostic);
  diagnostic_json = args.diagnostic_json;
  if (status != CONFIT_OK) {
    confit_cli_v2_args_clear(&args);
    return confit_cli_v2_return_error(status, &diagnostic, diagnostic_json);
  }
  if (strcmp(command, "check") == 0) status = confit_cli_v2_run_check(&args, &diagnostic);
  else if (strcmp(command, "resolve") == 0) status = confit_cli_v2_run_resolve(&args, &diagnostic);
  else if (strcmp(command, "gen") == 0) status = confit_cli_v2_run_gen(&args, &diagnostic);
  else if (strcmp(command, "explain") == 0) status = confit_cli_v2_run_explain(&args, &diagnostic);
  else if (strcmp(command, "list") == 0) status = confit_cli_v2_run_list(&args, &diagnostic);
  else if (strcmp(command, "graph") == 0) status = confit_cli_v2_run_graph(&args, &diagnostic);
  else if (strcmp(command, "component") == 0) status = confit_cli_v2_run_component(&args, &diagnostic);
  else status = confit_cli_v2_run_diff(&args, &diagnostic);
  if (out_handled != 0) *out_handled = 1;
  confit_cli_v2_args_clear(&args);
  return status != CONFIT_OK
             ? confit_cli_v2_return_error(status, &diagnostic, diagnostic_json)
             : confit_status_exit_code(CONFIT_OK);
}
