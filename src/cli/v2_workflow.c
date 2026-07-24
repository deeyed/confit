#include "v2_workflow.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/compat_v2.h"
#include "confit/constraint_v2.h"
#include "confit/generator_v2.h"
#include "confit/host.h"
#include "confit/migration_v2.h"
#include "confit/parser_v2.h"
#include "confit/resolver_v2.h"
#include "confit/schema_v2.h"
#include "confit/status.h"

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
  const char *compat;
  const char *parus;
  const char *delos;
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

static const char kInvalidCommand[] = "invalid schema v2 command arguments";
static const char kInvalidOption[] = "unknown schema v2 command option";
static const char kMissingProject[] = "schema v2 command requires --project";
static const char kUnsupportedArtifact[] =
    "schema v2 gen currently publishes its complete deterministic artifact set; use --artifact all";
static const char kMixedCompat[] =
    "v1 and v2 projects cannot be mixed by the compatibility command";

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
  args->artifact = "all";
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

static ConfitStatus confit_cli_v2_parse(const char *command, int argc,
                                         char **argv, ConfitCliV2Args *args,
                                         ConfitDiagnostic *diagnostic) {
  int index;

  confit_cli_v2_args_init(args);
  for (index = 2; index < argc; ++index) {
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
         strcmp(arg, "--compat") == 0 || strcmp(arg, "--parus") == 0 ||
         strcmp(arg, "--delos") == 0 ||
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
      else if (strcmp(arg, "--compat") == 0) args->compat = value;
      else if (strcmp(arg, "--parus") == 0) args->parus = value;
      else if (strcmp(arg, "--delos") == 0) args->delos = value;
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
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          kInvalidCommand);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  return CONFIT_OK;
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
  ConfitStatus status = confit_cli_v2_context_load(args, &context, diagnostic);
  if (status == CONFIT_OK) status = confit_host_stdout_write_line("check ok");
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
  ConfitV2ArtifactOptions options;
  ConfitV2ArtifactSet artifacts;
  size_t changed = 0U;
  ConfitStatus status;

  if (args->out == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          "schema v2 gen requires --out");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  if (strcmp(args->artifact, "all") != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_UNSUPPORTED, args->artifact,
                          0U, 0U, kUnsupportedArtifact);
    return CONFIT_ERR_UNSUPPORTED;
  }
  status = confit_cli_v2_context_load(args, &context, diagnostic);
  memset(&artifacts, 0, sizeof(artifacts));
  memset(&options, 0, sizeof(options));
  if (status == CONFIT_OK) {
    options.compiled = context.compiled;
    status = confit_v2_generate_artifacts(context.snapshot, &options, &artifacts,
                                           diagnostic);
  }
  if (status == CONFIT_OK && args->dry_run) {
    status = confit_host_stdout_write_line("gen dry-run ok");
  } else if (status == CONFIT_OK) {
    status = confit_v2_write_artifacts(args->out, &artifacts, &changed, diagnostic);
  }
  if (status == CONFIT_OK && !args->dry_run) {
    char line[128];
    (void)snprintf(line, sizeof(line), "gen ok: %llu file(s) changed",
                   (unsigned long long)changed);
    status = confit_host_stdout_write_line(line);
  }
  confit_v2_artifact_set_clear(&artifacts);
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

static ConfitStatus confit_cli_v2_run_compat(const ConfitCliV2Args *args,
                                              ConfitDiagnostic *diagnostic) {
  ConfitCliV2Args parus_args = *args;
  ConfitCliV2Args delos_args = *args;
  ConfitCliV2Context parus;
  ConfitCliV2Context delos;
  ConfitV2CompatSuite *suite = 0;
  ConfitV2CompatReport *report = 0;
  ConfitV2CompatProject projects[2];
  char *text = 0;
  ConfitStatus status;

  memset(&parus, 0, sizeof(parus));
  memset(&delos, 0, sizeof(delos));

  if (args->parus == 0 || args->delos == 0 || args->compat == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          "schema v2 compat requires --parus, --delos, and --compat");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  parus_args.project = args->parus;
  delos_args.project = args->delos;
  status = confit_cli_v2_context_load(&parus_args, &parus, diagnostic);
  if (status == CONFIT_OK) status = confit_cli_v2_context_load(&delos_args, &delos, diagnostic);
  if (status == CONFIT_OK) status = confit_v2_compat_load_file(args->compat, &suite, diagnostic);
  memset(projects, 0, sizeof(projects));
  if (status == CONFIT_OK) {
    projects[0].alias = "parus";
    projects[0].snapshot = parus.snapshot;
    projects[0].schema_version = CONFIT_V2_COMPAT_SCHEMA_VERSION;
    projects[0].artifact_abi = CONFIT_V2_COMPAT_ARTIFACT_ABI;
    projects[1].alias = "delos";
    projects[1].snapshot = delos.snapshot;
    projects[1].schema_version = CONFIT_V2_COMPAT_SCHEMA_VERSION;
    projects[1].artifact_abi = CONFIT_V2_COMPAT_ARTIFACT_ABI;
    status = confit_v2_compat_check(suite, projects, 2U, &report, diagnostic);
  }
  if ((status == CONFIT_OK || status == CONFIT_ERR_COMPATIBILITY) && report != 0) {
    ConfitStatus render = strcmp(args->format, "json") == 0
                              ? confit_v2_compat_report_to_json(report, &text)
                              : confit_v2_compat_report_to_text(report, &text);
    if (render == CONFIT_OK) render = confit_host_stdout_write(text);
    if (render != CONFIT_OK && status == CONFIT_OK) status = render;
  }
  free(text);
  confit_v2_compat_report_free(report);
  confit_v2_compat_suite_free(suite);
  confit_cli_v2_context_clear(&delos);
  confit_cli_v2_context_clear(&parus);
  return status;
}

static ConfitStatus confit_cli_v2_run_migrate(const ConfitCliV2Args *args,
                                               ConfitDiagnostic *diagnostic) {
  ConfitV1ToV2MigrationOptions options;
  ConfitStatus status;

  if (args->project == 0 || args->out == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          "migrate requires --project and --out");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  options.source_project_root = args->project;
  options.output_root = args->out;
  status = confit_v1_migrate_to_v2_candidate(&options, diagnostic);
  if (status == CONFIT_OK) status = confit_host_stdout_write_line("migration candidate ok");
  return status;
}

static ConfitSchemaVersion confit_cli_v2_project_version(const char *root,
                                                          ConfitDiagnostic *diagnostic) {
  ConfitV2TomlDocument *document = 0;
  const ConfitV2TomlValue *document_root;
  const ConfitV2TomlValue *project;
  const ConfitV2TomlValue *schema_version;
  char direct_path[4096];
  char config_root[4096];
  char project_path[4096];
  ConfitSchemaVersion version = CONFIT_SCHEMA_VERSION_INVALID;
  int64_t raw_version;

  if (root == 0) return CONFIT_SCHEMA_VERSION_INVALID;
  if (confit_host_path_join(direct_path, sizeof(direct_path), root,
                            "project.toml", diagnostic) != CONFIT_OK) {
    return version;
  }
  if (confit_host_file_exists(direct_path)) {
    memcpy(project_path, direct_path, strlen(direct_path) + 1U);
  } else if (confit_host_path_join(config_root, sizeof(config_root), root,
                                   "config", diagnostic) != CONFIT_OK ||
             confit_host_path_join(project_path, sizeof(project_path), config_root,
                                   "project.toml", diagnostic) != CONFIT_OK) {
    return version;
  }
  if (confit_v2_toml_parse_file(project_path, &document, diagnostic) != CONFIT_OK) {
    return version;
  }
  document_root = confit_v2_toml_document_root(document);
  project = confit_v2_toml_table_find(document_root, "project");
  schema_version = project != 0 ? confit_v2_toml_table_find(project, "schema_version") : 0;
  if (schema_version != 0 && confit_v2_toml_value_int64(schema_version, &raw_version)) {
    if (raw_version == (int64_t)CONFIT_SCHEMA_VERSION_V1) {
      version = CONFIT_SCHEMA_VERSION_V1;
    } else if (raw_version == (int64_t)CONFIT_SCHEMA_VERSION_V2) {
      version = CONFIT_SCHEMA_VERSION_V2;
    }
  }
  confit_v2_toml_document_free(document);
  return version;
}

int confit_cli_v2_try_run(const char *command, int argc, char **argv,
                          int *out_handled) {
  ConfitCliV2Args args;
  ConfitDiagnostic diagnostic;
  ConfitSchemaVersion version;
  ConfitStatus status;
  int handled = 0;
  int diagnostic_json = 0;

  if (out_handled != 0) *out_handled = 0;
  if (strcmp(command, "migrate") != 0 && strcmp(command, "compat") != 0 &&
      strcmp(command, "check") != 0 && strcmp(command, "resolve") != 0 &&
      strcmp(command, "gen") != 0 && strcmp(command, "explain") != 0 &&
      strcmp(command, "list") != 0 && strcmp(command, "graph") != 0 &&
      strcmp(command, "diff") != 0) {
    return 0;
  }
  confit_diagnostic_init(&diagnostic);
  status = confit_cli_v2_parse(command, argc, argv, &args, &diagnostic);
  diagnostic_json = args.diagnostic_json;
  if (status != CONFIT_OK) {
    confit_cli_v2_args_clear(&args);
    return confit_cli_v2_return_error(status, &diagnostic, diagnostic_json);
  }
  if (strcmp(command, "migrate") == 0) {
    handled = 1;
    status = confit_cli_v2_run_migrate(&args, &diagnostic);
  } else if (strcmp(command, "compat") == 0) {
    const ConfitSchemaVersion parus = confit_cli_v2_project_version(args.parus, &diagnostic);
    const ConfitSchemaVersion delos = confit_cli_v2_project_version(args.delos, &diagnostic);
    if (parus == CONFIT_SCHEMA_VERSION_V2 || delos == CONFIT_SCHEMA_VERSION_V2) {
      handled = 1;
      status = parus == CONFIT_SCHEMA_VERSION_V2 && delos == CONFIT_SCHEMA_VERSION_V2
                   ? confit_cli_v2_run_compat(&args, &diagnostic)
                   : CONFIT_ERR_SCHEMA;
      if (status == CONFIT_ERR_SCHEMA && (parus != CONFIT_SCHEMA_VERSION_V2 ||
                                          delos != CONFIT_SCHEMA_VERSION_V2)) {
        confit_diagnostic_set(&diagnostic, status, 0, 0U, 0U, kMixedCompat);
      }
    }
  } else if (strcmp(command, "check") == 0 || strcmp(command, "resolve") == 0 ||
             strcmp(command, "gen") == 0 || strcmp(command, "explain") == 0 ||
             strcmp(command, "list") == 0 || strcmp(command, "graph") == 0 ||
             strcmp(command, "diff") == 0) {
    version = confit_cli_v2_project_version(args.project, &diagnostic);
    if (version == CONFIT_SCHEMA_VERSION_V2) {
      handled = 1;
      if (strcmp(command, "check") == 0) status = confit_cli_v2_run_check(&args, &diagnostic);
      else if (strcmp(command, "resolve") == 0) status = confit_cli_v2_run_resolve(&args, &diagnostic);
      else if (strcmp(command, "gen") == 0) status = confit_cli_v2_run_gen(&args, &diagnostic);
      else if (strcmp(command, "explain") == 0) status = confit_cli_v2_run_explain(&args, &diagnostic);
      else if (strcmp(command, "list") == 0) status = confit_cli_v2_run_list(&args, &diagnostic);
      else if (strcmp(command, "graph") == 0) status = confit_cli_v2_run_graph(&args, &diagnostic);
      else status = confit_cli_v2_run_diff(&args, &diagnostic);
    }
  }
  if (out_handled != 0) *out_handled = handled;
  confit_cli_v2_args_clear(&args);
  return handled && status != CONFIT_OK
             ? confit_cli_v2_return_error(status, &diagnostic, diagnostic_json)
             : handled ? confit_status_exit_code(CONFIT_OK) : 0;
}
