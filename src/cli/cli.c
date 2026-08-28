#include "cli_internal.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "confit/config.h"
#include "confit/diagnostic.h"
#include "confit/emitter.h"
#include "confit/host.h"
#include "confit/limits.h"
#include "confit/model.h"
#include "confit/resolver.h"
#include "confit/schema.h"
#include "confit/snapshot.h"
#include "confit/status.h"
#include "confit/version.h"

typedef enum ConfitCliCommand {
  CONFIT_CLI_COMMAND_INVALID = 0,
  CONFIT_CLI_COMMAND_CHECK,
  CONFIT_CLI_COMMAND_CONFIGURE,
  CONFIT_CLI_COMMAND_MENUCONFIG,
  CONFIT_CLI_COMMAND_VERIFY,
  CONFIT_CLI_COMMAND_SEARCH,
  CONFIT_CLI_COMMAND_EXPLAIN,
  CONFIT_CLI_COMMAND_DIFF,
  CONFIT_CLI_COMMAND_LISTNEWCONFIG,
  CONFIT_CLI_COMMAND_OLDCONFIG,
  CONFIT_CLI_COMMAND_OLDDEFCONFIG,
  CONFIT_CLI_COMMAND_SAVEDEFCONFIG,
} ConfitCliCommand;

enum {
  CONFIT_CLI_OPTION_ROOT = 1U << 0,
  CONFIT_CLI_OPTION_PROJECT = 1U << 1,
  CONFIT_CLI_OPTION_CONFIG = 1U << 2,
  CONFIT_CLI_OPTION_OTHER_CONFIG = 1U << 3,
  CONFIT_CLI_OPTION_OUTPUT = 1U << 4,
  CONFIT_CLI_OPTION_EMIT = 1U << 5,
  CONFIT_CLI_OPTION_PRINT_ARTIFACT = 1U << 6,
  CONFIT_CLI_OPTION_QUERY = 1U << 7,
  CONFIT_CLI_OPTION_SYMBOL = 1U << 8,
  CONFIT_CLI_OPTION_DESTINATION = 1U << 9,
};

typedef struct ConfitCliCommandSpec {
  const char *name;
  ConfitCliCommand command;
  unsigned required;
  unsigned allowed;
} ConfitCliCommandSpec;

typedef struct ConfitCliOptions {
  const ConfitCliCommandSpec *spec;
  const char *root;
  const char *project;
  const char *config;
  const char *other_config;
  const char *output;
  const char *print_artifact;
  const char *query;
  const char *symbol;
  const char *destination;
  ConfitEmitRequest emit;
  unsigned seen;
} ConfitCliOptions;

typedef struct ConfitCliLoaded {
  ConfitHostRoot *project_root;
  ConfitSchemaProject *project;
  ConfitUserConfig *config;
  ConfitResolution *resolution;
} ConfitCliLoaded;

#define CLI_BASE_OPTIONS                                                     \
  (CONFIT_CLI_OPTION_ROOT | CONFIT_CLI_OPTION_PROJECT)

static const ConfitCliCommandSpec kConfitCliCommands[] = {
    {"check", CONFIT_CLI_COMMAND_CHECK, CLI_BASE_OPTIONS,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_CONFIG},
    {"configure", CONFIT_CLI_COMMAND_CONFIGURE,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_OUTPUT,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_CONFIG | CONFIT_CLI_OPTION_OUTPUT |
         CONFIT_CLI_OPTION_EMIT},
    {"menuconfig", CONFIT_CLI_COMMAND_MENUCONFIG,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_OUTPUT,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_CONFIG | CONFIT_CLI_OPTION_OUTPUT |
         CONFIT_CLI_OPTION_EMIT},
    {"verify", CONFIT_CLI_COMMAND_VERIFY,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_OUTPUT,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_OUTPUT |
         CONFIT_CLI_OPTION_PRINT_ARTIFACT},
    {"search", CONFIT_CLI_COMMAND_SEARCH,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_QUERY,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_CONFIG | CONFIT_CLI_OPTION_QUERY},
    {"explain", CONFIT_CLI_COMMAND_EXPLAIN,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_SYMBOL,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_CONFIG | CONFIT_CLI_OPTION_SYMBOL},
    {"diff", CONFIT_CLI_COMMAND_DIFF,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_OTHER_CONFIG,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_CONFIG |
         CONFIT_CLI_OPTION_OTHER_CONFIG},
    {"listnewconfig", CONFIT_CLI_COMMAND_LISTNEWCONFIG,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_OUTPUT,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_CONFIG | CONFIT_CLI_OPTION_OUTPUT},
    {"oldconfig", CONFIT_CLI_COMMAND_OLDCONFIG,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_OUTPUT,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_CONFIG | CONFIT_CLI_OPTION_OUTPUT |
         CONFIT_CLI_OPTION_EMIT},
    {"olddefconfig", CONFIT_CLI_COMMAND_OLDDEFCONFIG,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_OUTPUT,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_CONFIG | CONFIT_CLI_OPTION_OUTPUT |
         CONFIT_CLI_OPTION_EMIT},
    {"savedefconfig", CONFIT_CLI_COMMAND_SAVEDEFCONFIG,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_OUTPUT |
         CONFIT_CLI_OPTION_DESTINATION,
     CLI_BASE_OPTIONS | CONFIT_CLI_OPTION_OUTPUT |
         CONFIT_CLI_OPTION_DESTINATION},
};

static const char kCliInvalidArguments[] = "invalid command arguments";
static const char kCliUnknownSymbol[] = "configuration symbol was not found";
static const char kCliTerminalUnavailable[] =
    "terminal menuconfig is not available until the terminal frontend round";
static const char kCliMigrationUnavailable[] =
    "configuration migration commands are not implemented until the migration round";
static const char kCliOutputFailed[] = "failed to write command output";
static const char kCliInternal[] = "configuration command invariant failed";

static const ConfitCliCommandSpec *confit_cli_find_command(const char *name) {
  size_t index;
  for (index = 0U;
       index < sizeof(kConfitCliCommands) / sizeof(kConfitCliCommands[0]);
       ++index) {
    if (strcmp(kConfitCliCommands[index].name, name) == 0)
      return &kConfitCliCommands[index];
  }
  return 0;
}

static void confit_cli_print_help(void) {
  (void)fputs(
      "Confit generic configuration tool (schema 6)\n\n"
      "Usage: confit <command> [options]\n\n"
      "Commands:\n"
      "  help             Show this help text\n"
      "  check            Validate and resolve without writing\n"
      "  configure        Resolve and publish an immutable snapshot\n"
      "  menuconfig       Terminal editor (reserved; unavailable)\n"
      "  verify           Verify the selected immutable snapshot\n"
      "  search           Search prompts, help text, and symbols\n"
      "  explain          Explain one resolved symbol\n"
      "  diff             Compare two user configurations\n"
      "  listnewconfig    Migration command (reserved; unavailable)\n"
      "  oldconfig        Migration command (reserved; unavailable)\n"
      "  olddefconfig     Migration command (reserved; unavailable)\n"
      "  savedefconfig    Migration command (reserved; unavailable)\n\n"
      "Options:\n"
      "  --root ABSOLUTE_PROJECT_ROOT\n"
      "  --project RELATIVE_ENTRY_TOML\n"
      "  --config USER_CONFIG\n"
      "  --other-config USER_CONFIG\n"
      "  --output ABSOLUTE_OUTPUT_ROOT\n"
      "  --emit make|c-header|json\n"
      "  --print-artifact NAME\n"
      "  --query TEXT\n"
      "  --symbol SYMBOL\n"
      "  --destination PATH\n\n"
      "Commands accept only the options assigned to them. No project, profile,\n"
      "or configuration path is discovered from the environment.\n",
      stdout);
}

static int confit_cli_usage(const char *command, const char *message) {
  (void)fprintf(stderr, "confit: %s: usage error: %s\n",
                command != 0 ? command : "command", message);
  return confit_status_exit_code(CONFIT_ERR_USAGE);
}

static void confit_cli_report(const char *command, ConfitStatus status,
                              const ConfitDiagnostic *diagnostic) {
  const char *message = diagnostic != 0 && diagnostic->message != 0
                            ? diagnostic->message
                            : confit_status_name(status);
  if (diagnostic != 0 && diagnostic->path != 0) {
    (void)fprintf(stderr, "%s:%zu:%zu: error: %s\n", diagnostic->path,
                  diagnostic->line, diagnostic->column, message);
  } else {
    (void)fprintf(stderr, "confit: %s: error: %s\n", command, message);
  }
}

static int confit_cli_status(const char *command, ConfitStatus status,
                             const ConfitDiagnostic *diagnostic) {
  if (status != CONFIT_OK) confit_cli_report(command, status, diagnostic);
  return confit_status_exit_code(status);
}

static int confit_cli_suffix(const char *path, const char *suffix) {
  const size_t path_size = path != 0 ? strlen(path) : 0U;
  const size_t suffix_size = strlen(suffix);
  return path_size >= suffix_size &&
         strcmp(path + path_size - suffix_size, suffix) == 0;
}

static int confit_cli_config_path(const char *path) {
  return path != 0 && confit_cli_suffix(path, ".toml") &&
         (confit_host_relative_path_is_valid(path) ||
          confit_host_absolute_path_is_valid(path));
}

static int confit_cli_utf8_continuation(unsigned char byte) {
  return (byte & 0xC0U) == 0x80U;
}

static int confit_cli_query_is_valid(const char *text) {
  const unsigned char *bytes = (const unsigned char *)text;
  size_t size = 0U;
  size_t index = 0U;
  if (text == 0) return 0;
  while (size <= CONFIT_LIMIT_STRING_BYTES && text[size] != '\0') ++size;
  if (size == 0U || size > CONFIT_LIMIT_STRING_BYTES) return 0;
  while (index < size) {
    const unsigned char first = bytes[index];
    size_t width;
    if (first < 0x20U || first == 0x7FU) return 0;
    if (first < 0x80U)
      width = 1U;
    else if (first >= 0xC2U && first <= 0xDFU)
      width = 2U;
    else if (first >= 0xE0U && first <= 0xEFU)
      width = 3U;
    else if (first >= 0xF0U && first <= 0xF4U)
      width = 4U;
    else
      return 0;
    if (index + width > size ||
        (width >= 2U && !confit_cli_utf8_continuation(bytes[index + 1U])) ||
        (width >= 3U && !confit_cli_utf8_continuation(bytes[index + 2U])) ||
        (width >= 4U && !confit_cli_utf8_continuation(bytes[index + 3U])) ||
        (width == 3U && first == 0xE0U && bytes[index + 1U] < 0xA0U) ||
        (width == 3U && first == 0xEDU && bytes[index + 1U] > 0x9FU) ||
        (width == 4U && first == 0xF0U && bytes[index + 1U] < 0x90U) ||
        (width == 4U && first == 0xF4U && bytes[index + 1U] > 0x8FU))
      return 0;
    if (width == 2U && first == 0xC2U && bytes[index + 1U] >= 0x80U &&
        bytes[index + 1U] <= 0x9FU)
      return 0;
    index += width;
  }
  return 1;
}

static int confit_cli_artifact_name(const char *name) {
  static const char *const names[] = {
      "user-values.toml", "resolved-values.json", "inputs.manifest",
      "provenance.json", "snapshot.seal",        "values.mk",
      "values.h",
  };
  size_t index;
  if (name == 0) return 0;
  for (index = 0U; index < sizeof(names) / sizeof(names[0]); ++index)
    if (strcmp(name, names[index]) == 0) return 1;
  return 0;
}

static int confit_cli_parse_emit(ConfitCliOptions *options,
                                 const char *value) {
  int *field = 0;
  if (strcmp(value, "make") == 0)
    field = &options->emit.emit_make;
  else if (strcmp(value, "c-header") == 0)
    field = &options->emit.emit_c_header;
  else if (strcmp(value, "json") == 0)
    field = &options->emit.emit_json;
  if (field == 0 || *field != 0) return 0;
  *field = 1;
  options->seen |= CONFIT_CLI_OPTION_EMIT;
  return 1;
}

static int confit_cli_set_option(ConfitCliOptions *options, const char *name,
                                 const char *value) {
  const char **destination = 0;
  unsigned bit = 0U;
  if (strcmp(name, "--emit") == 0) return confit_cli_parse_emit(options, value);
  if (strcmp(name, "--root") == 0) {
    bit = CONFIT_CLI_OPTION_ROOT;
    destination = &options->root;
  } else if (strcmp(name, "--project") == 0) {
    bit = CONFIT_CLI_OPTION_PROJECT;
    destination = &options->project;
  } else if (strcmp(name, "--config") == 0) {
    bit = CONFIT_CLI_OPTION_CONFIG;
    destination = &options->config;
  } else if (strcmp(name, "--other-config") == 0) {
    bit = CONFIT_CLI_OPTION_OTHER_CONFIG;
    destination = &options->other_config;
  } else if (strcmp(name, "--output") == 0) {
    bit = CONFIT_CLI_OPTION_OUTPUT;
    destination = &options->output;
  } else if (strcmp(name, "--print-artifact") == 0) {
    bit = CONFIT_CLI_OPTION_PRINT_ARTIFACT;
    destination = &options->print_artifact;
  } else if (strcmp(name, "--query") == 0) {
    bit = CONFIT_CLI_OPTION_QUERY;
    destination = &options->query;
  } else if (strcmp(name, "--symbol") == 0) {
    bit = CONFIT_CLI_OPTION_SYMBOL;
    destination = &options->symbol;
  } else if (strcmp(name, "--destination") == 0) {
    bit = CONFIT_CLI_OPTION_DESTINATION;
    destination = &options->destination;
  } else {
    return 0;
  }
  if ((options->seen & bit) != 0U) return 0;
  options->seen |= bit;
  *destination = value;
  return 1;
}

static int confit_cli_validate_options(const ConfitCliOptions *options) {
  if ((options->seen & options->spec->required) != options->spec->required ||
      (options->seen & ~options->spec->allowed) != 0U)
    return 0;
  if (options->root != 0 &&
      !confit_host_absolute_path_is_valid(options->root))
    return 0;
  if (options->project != 0 &&
      (!confit_host_relative_path_is_valid(options->project) ||
       !confit_cli_suffix(options->project, ".toml")))
    return 0;
  if ((options->config != 0 && !confit_cli_config_path(options->config)) ||
      (options->other_config != 0 &&
       !confit_cli_config_path(options->other_config)))
    return 0;
  if (options->output != 0 &&
      !confit_host_absolute_path_is_valid(options->output))
    return 0;
  if (options->print_artifact != 0 &&
      !confit_cli_artifact_name(options->print_artifact))
    return 0;
  if (options->query != 0 && !confit_cli_query_is_valid(options->query))
    return 0;
  if (options->symbol != 0 && !confit_symbol_is_valid(options->symbol))
    return 0;
  if (options->destination != 0 &&
      !confit_cli_config_path(options->destination))
    return 0;
  return 1;
}

static int confit_cli_parse(int argc, char **argv, ConfitCliOptions *options) {
  int index;
  memset(options, 0, sizeof(*options));
  options->spec = confit_cli_find_command(argv[1]);
  if (options->spec == 0) return 0;
  for (index = 2; index < argc; index += 2) {
    if (index + 1 >= argc || strncmp(argv[index], "--", 2U) != 0 ||
        !confit_cli_set_option(options, argv[index], argv[index + 1]))
      return 0;
  }
  return confit_cli_validate_options(options);
}

static void confit_cli_loaded_init(ConfitCliLoaded *loaded) {
  memset(loaded, 0, sizeof(*loaded));
}

static void confit_cli_loaded_destroy(ConfitCliLoaded *loaded) {
  if (loaded == 0) return;
  confit_resolution_destroy(loaded->resolution);
  confit_user_config_destroy(loaded->config);
  confit_schema_project_destroy(loaded->project);
  confit_host_root_destroy(loaded->project_root);
  confit_cli_loaded_init(loaded);
}

static ConfitStatus confit_cli_load_user(
    ConfitHostRoot *project_root, const ConfitCatalog *catalog,
    const char *path, ConfitUserConfig **out_config,
    ConfitDiagnostic *diagnostic) {
  if (path == 0) {
    *out_config = 0;
    return CONFIT_OK;
  }
  if (path[0] == '/')
    return confit_user_config_load_absolute(path, catalog, 0, out_config,
                                            diagnostic);
  return confit_user_config_load_relative(project_root, path, catalog, 0,
                                          out_config, diagnostic);
}

static ConfitStatus confit_cli_resolve_one(
    ConfitHostRoot *project_root, const ConfitSchemaProject *project,
    const char *config_path, ConfitUserConfig **out_config,
    ConfitResolution **out_resolution, ConfitDiagnostic *diagnostic) {
  const ConfitAssignment *assignments = 0;
  size_t assignment_count = 0U;
  ConfitStatus status;
  *out_config = 0;
  *out_resolution = 0;
  status = confit_cli_load_user(project_root,
                                confit_schema_project_catalog(project),
                                config_path, out_config, diagnostic);
  if (status != CONFIT_OK) return status;
  assignments = confit_user_config_assignments(*out_config, &assignment_count);
  status = confit_resolve(confit_schema_project_catalog(project),
                          confit_schema_project_dependency_plan(project),
                          assignments, assignment_count, 0, out_resolution,
                          diagnostic);
  if (status != CONFIT_OK) {
    confit_user_config_destroy(*out_config);
    *out_config = 0;
  }
  return status;
}

static ConfitStatus confit_cli_load(const ConfitCliOptions *options,
                                    ConfitCliLoaded *loaded,
                                    ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  confit_cli_loaded_init(loaded);
  status = confit_host_root_open_absolute(options->root, 0,
                                          &loaded->project_root, diagnostic);
  if (status == CONFIT_OK)
    status = confit_schema_project_load(loaded->project_root, options->project,
                                        0, &loaded->project, diagnostic);
  if (status == CONFIT_OK)
    status = confit_cli_resolve_one(
        loaded->project_root, loaded->project, options->config,
        &loaded->config, &loaded->resolution, diagnostic);
  if (status != CONFIT_OK) confit_cli_loaded_destroy(loaded);
  return status;
}

static const char *confit_cli_kind_name(ConfitValueKind kind) {
  switch (kind) {
  case CONFIT_VALUE_BOOL:
    return "bool";
  case CONFIT_VALUE_INT:
    return "int";
  case CONFIT_VALUE_HEX:
    return "hex";
  case CONFIT_VALUE_STRING:
    return "string";
  case CONFIT_VALUE_ENUM:
    return "enum";
  case CONFIT_VALUE_INVALID:
  default:
    return "invalid";
  }
}

static int confit_cli_append(char *out, size_t capacity, size_t *used,
                             const char *text, size_t size) {
  if (out == 0 || used == 0 || text == 0 || *used >= capacity ||
      size >= capacity - *used)
    return 0;
  memcpy(out + *used, text, size);
  *used += size;
  out[*used] = '\0';
  return 1;
}

static int confit_cli_format_text(const char *text, size_t size, char *out,
                                  size_t capacity) {
  static const char digits[] = "0123456789abcdef";
  size_t used = 0U;
  size_t index;
  if (!confit_cli_append(out, capacity, &used, "\"", 1U)) return 0;
  for (index = 0U; index < size; ++index) {
    const unsigned char byte = (unsigned char)text[index];
    if (byte == '\\' || byte == '"') {
      const char escaped[2] = {'\\', (char)byte};
      if (!confit_cli_append(out, capacity, &used, escaped, 2U)) return 0;
    } else if (byte == '\n' || byte == '\r' || byte == '\t') {
      const char escaped[2] = {'\\', byte == '\n' ? 'n' :
                                      byte == '\r' ? 'r' : 't'};
      if (!confit_cli_append(out, capacity, &used, escaped, 2U)) return 0;
    } else if (byte < 0x20U || byte == 0x7FU) {
      const char escaped[4] = {'\\', 'x', digits[byte >> 4U],
                               digits[byte & 0x0fU]};
      if (!confit_cli_append(out, capacity, &used, escaped, 4U)) return 0;
    } else if (!confit_cli_append(out, capacity, &used, (const char *)&text[index],
                                  1U)) {
      return 0;
    }
  }
  return confit_cli_append(out, capacity, &used, "\"", 1U);
}

static int confit_cli_format_value(const ConfitValue *value, char *out,
                                   size_t capacity) {
  const char *text = 0;
  size_t size = 0U;
  int written;
  if (value == 0 || out == 0 || capacity == 0U) return 0;
  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    written = snprintf(out, capacity, "%s",
                       value->data.boolean != 0 ? "true" : "false");
    break;
  case CONFIT_VALUE_INT:
    written = snprintf(out, capacity, "%" PRId64, value->data.integer);
    break;
  case CONFIT_VALUE_HEX:
    written = snprintf(out, capacity, "0x%" PRIx64,
                       value->data.hexadecimal);
    break;
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
    if (!confit_value_text(value, &text, &size)) return 0;
    return confit_cli_format_text(text, size, out, capacity);
  case CONFIT_VALUE_INVALID:
  default:
    return 0;
  }
  return written >= 0 && (size_t)written < capacity;
}

static unsigned char confit_cli_ascii_fold(unsigned char byte) {
  if (byte >= 'A' && byte <= 'Z') return (unsigned char)(byte + ('a' - 'A'));
  return byte;
}

static int confit_cli_contains_folded(const char *text, const char *query) {
  size_t text_size;
  size_t query_size;
  size_t start;
  if (text == 0 || query == 0) return 0;
  text_size = strlen(text);
  query_size = strlen(query);
  if (query_size > text_size) return 0;
  for (start = 0U; start + query_size <= text_size; ++start) {
    size_t index;
    for (index = 0U; index < query_size; ++index) {
      if (confit_cli_ascii_fold((unsigned char)text[start + index]) !=
          confit_cli_ascii_fold((unsigned char)query[index]))
        break;
    }
    if (index == query_size) return 1;
  }
  return 0;
}

static ConfitStatus confit_cli_command_check(const ConfitCliOptions *options,
                                             ConfitDiagnostic *diagnostic) {
  ConfitCliLoaded loaded;
  ConfitStatus status = confit_cli_load(options, &loaded, diagnostic);
  if (status == CONFIT_OK && fputs("configuration is valid\n", stdout) == EOF) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_IO, 0, 0U, 0U,
                          kCliOutputFailed);
    status = CONFIT_ERR_IO;
  }
  confit_cli_loaded_destroy(&loaded);
  return status;
}

static ConfitStatus confit_cli_command_configure(
    const ConfitCliOptions *options, ConfitDiagnostic *diagnostic) {
  ConfitCliLoaded loaded;
  ConfitHostRoot *output_root = 0;
  ConfitEmission *emission = 0;
  ConfitSnapshotArtifactSpec artifacts[2];
  ConfitSnapshotPublishRequest request;
  ConfitSnapshotPublication publication;
  size_t artifact_count = 0U;
  size_t index;
  ConfitStatus status = confit_cli_load(options, &loaded, diagnostic);
  memset(artifacts, 0, sizeof(artifacts));
  memset(&request, 0, sizeof(request));
  memset(&publication, 0, sizeof(publication));
  if (status == CONFIT_OK && (options->seen & CONFIT_CLI_OPTION_EMIT) != 0U)
    status = confit_emit(loaded.resolution, &options->emit, 0, &emission,
                         diagnostic);
  for (index = 0U; status == CONFIT_OK && emission != 0 &&
                  index < confit_emission_artifact_count(emission);
       ++index) {
    ConfitEmittedArtifactView view;
    if (!confit_emission_artifact_at(emission, index, &view)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0U, 0U,
                            kCliInternal);
      status = CONFIT_ERR_INTERNAL;
      break;
    }
    if (view.kind == CONFIT_EMITTER_JSON) continue;
    if (artifact_count >= sizeof(artifacts) / sizeof(artifacts[0])) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0U, 0U,
                            kCliInternal);
      status = CONFIT_ERR_INTERNAL;
      break;
    }
    artifacts[artifact_count].role = view.role;
    artifacts[artifact_count].name = view.name;
    artifacts[artifact_count].bytes = view.bytes;
    artifacts[artifact_count].size = view.size;
    artifacts[artifact_count].printable = view.printable;
    artifact_count += 1U;
  }
  if (status == CONFIT_OK)
    status = confit_host_root_open_absolute(options->output, 0, &output_root,
                                            diagnostic);
  if (status == CONFIT_OK) {
    request.project = loaded.project;
    request.user_config = loaded.config;
    request.resolution = loaded.resolution;
    request.optional_artifacts = artifacts;
    request.optional_artifact_count = artifact_count;
    request.resolved_values_printable = options->emit.emit_json;
    status = confit_snapshot_publish(output_root, &request, 0, &publication,
                                     diagnostic);
  }
  if (status == CONFIT_OK &&
      fprintf(stdout, "configured snapshot %s\n", publication.digest) < 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_IO, 0, 0U, 0U,
                          kCliOutputFailed);
    status = CONFIT_ERR_IO;
  }
  confit_host_root_destroy(output_root);
  confit_emission_destroy(emission);
  confit_cli_loaded_destroy(&loaded);
  return status;
}

static ConfitStatus confit_cli_join_artifact(const char *output,
                                             const char *relative, char *out,
                                             size_t out_size) {
  const size_t output_size = strlen(output);
  const size_t relative_size = strlen(relative);
  const size_t separator = strcmp(output, "/") == 0 ? 0U : 1U;
  if (output_size + separator + relative_size + 1U > out_size)
    return CONFIT_ERR_INTERNAL;
  memcpy(out, output, output_size);
  if (separator != 0U) out[output_size] = '/';
  memcpy(out + output_size + separator, relative, relative_size + 1U);
  return CONFIT_OK;
}

static ConfitStatus confit_cli_command_verify(const ConfitCliOptions *options,
                                              ConfitDiagnostic *diagnostic) {
  ConfitHostRoot *project_root = 0;
  ConfitHostRoot *output_root = 0;
  ConfitSnapshotVerifyRequest request;
  char relative[CONFIT_LIMIT_SOURCE_PATH_BYTES + 1U];
  char absolute[CONFIT_LIMIT_SOURCE_PATH_BYTES * 2U + 3U];
  ConfitStatus status;
  memset(&request, 0, sizeof(request));
  relative[0] = '\0';
  absolute[0] = '\0';
  status = confit_host_root_open_absolute(options->root, 0, &project_root,
                                          diagnostic);
  if (status == CONFIT_OK)
    status = confit_host_root_open_absolute(options->output, 0, &output_root,
                                            diagnostic);
  if (status == CONFIT_OK) {
    request.project_root = project_root;
    request.output_root = output_root;
    request.expected_entry_path = options->project;
    request.artifact_name = options->print_artifact;
    status = confit_snapshot_verify(
        &request, 0, options->print_artifact != 0 ? relative : 0,
        options->print_artifact != 0 ? sizeof(relative) : 0U, diagnostic);
  }
  if (status == CONFIT_OK && options->print_artifact != 0) {
    status = confit_cli_join_artifact(options->output, relative, absolute,
                                      sizeof(absolute));
    if (status == CONFIT_OK && fprintf(stdout, "%s\n", absolute) < 0)
      status = CONFIT_ERR_IO;
  } else if (status == CONFIT_OK &&
             fputs("configuration is current\n", stdout) == EOF) {
    status = CONFIT_ERR_IO;
  }
  if (status == CONFIT_ERR_IO && diagnostic->message == 0)
    confit_diagnostic_set(diagnostic, status, 0, 0U, 0U, kCliOutputFailed);
  if (status == CONFIT_ERR_INTERNAL && diagnostic->message == 0)
    confit_diagnostic_set(diagnostic, status, 0, 0U, 0U, kCliInternal);
  confit_host_root_destroy(output_root);
  confit_host_root_destroy(project_root);
  return status;
}

static ConfitStatus confit_cli_command_search(const ConfitCliOptions *options,
                                              ConfitDiagnostic *diagnostic) {
  ConfitCliLoaded loaded;
  size_t index;
  ConfitStatus status = confit_cli_load(options, &loaded, diagnostic);
  for (index = 0U; status == CONFIT_OK &&
                  index < confit_resolution_value_count(loaded.resolution);
       ++index) {
    const ConfitResolvedValue *resolved = 0;
    ConfitConfigView declaration;
    if (!confit_resolution_value_at(loaded.resolution, index, &resolved) ||
        resolved == 0 ||
        !confit_catalog_find_config(
            confit_schema_project_catalog(loaded.project), resolved->symbol,
            &declaration)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0U, 0U,
                            kCliInternal);
      status = CONFIT_ERR_INTERNAL;
      break;
    }
    if (confit_cli_contains_folded(declaration.symbol, options->query) ||
        confit_cli_contains_folded(declaration.prompt, options->query) ||
        confit_cli_contains_folded(declaration.help, options->query)) {
      if (fprintf(stdout, "%s\t%s\t%s\n", declaration.symbol,
                  confit_cli_kind_name(declaration.kind), declaration.prompt) <
          0) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_IO, 0, 0U, 0U,
                              kCliOutputFailed);
        status = CONFIT_ERR_IO;
      }
    }
  }
  confit_cli_loaded_destroy(&loaded);
  return status;
}

static ConfitStatus confit_cli_command_explain(const ConfitCliOptions *options,
                                               ConfitDiagnostic *diagnostic) {
  ConfitCliLoaded loaded;
  const ConfitResolvedValue *resolved = 0;
  ConfitConfigView declaration;
  char value[CONFIT_LIMIT_STRING_BYTES * 2U + 32U];
  char default_value[CONFIT_LIMIT_STRING_BYTES * 2U + 32U];
  ConfitStatus status = confit_cli_load(options, &loaded, diagnostic);
  if (status == CONFIT_OK &&
      (!confit_resolution_find_value(loaded.resolution, options->symbol,
                                     &resolved) ||
       !confit_catalog_find_config(confit_schema_project_catalog(loaded.project),
                                   options->symbol, &declaration))) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_VALIDATION, options->symbol,
                          0U, 0U, kCliUnknownSymbol);
    status = CONFIT_ERR_VALIDATION;
  }
  if (status == CONFIT_OK &&
      (!confit_cli_format_value(&resolved->effective_value, value,
                                sizeof(value)) ||
       !confit_cli_format_value(&resolved->default_value, default_value,
                                sizeof(default_value)))) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, options->symbol, 0U,
                          0U, kCliInternal);
    status = CONFIT_ERR_INTERNAL;
  }
  if (status == CONFIT_OK &&
      fprintf(stdout,
              "symbol: %s\ntype: %s\nprompt: %s\nvalue: %s\ndefault: %s\n"
              "origin: %s\navailable: %s\n",
              declaration.symbol, confit_cli_kind_name(declaration.kind),
              declaration.prompt, value, default_value,
              resolved->origin == CONFIT_ORIGIN_USER ? "user" : "default",
              resolved->available != 0 ? "true" : "false") < 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_IO, 0, 0U, 0U,
                          kCliOutputFailed);
    status = CONFIT_ERR_IO;
  }
  if (status == CONFIT_OK && resolved->reason != CONFIT_INDEX_NONE) {
    const ConfitReasonNode *reason = 0;
    char detail[CONFIT_LIMIT_DEPENDENCY_TEXT_BYTES * 2U + 32U];
    if (!confit_resolution_reason_at(loaded.resolution, resolved->reason,
                                     &reason) ||
        reason == 0 || reason->detail == 0 ||
        !confit_cli_format_text(reason->detail, strlen(reason->detail), detail,
                                sizeof(detail))) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, options->symbol,
                            0U, 0U, kCliInternal);
      status = CONFIT_ERR_INTERNAL;
    } else if (fprintf(stdout, "reason: %s\n", detail) < 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_IO, 0, 0U, 0U,
                            kCliOutputFailed);
      status = CONFIT_ERR_IO;
    }
  }
  confit_cli_loaded_destroy(&loaded);
  return status;
}

static ConfitStatus confit_cli_command_diff(const ConfitCliOptions *options,
                                            ConfitDiagnostic *diagnostic) {
  ConfitHostRoot *project_root = 0;
  ConfitSchemaProject *project = 0;
  ConfitUserConfig *left_config = 0;
  ConfitUserConfig *right_config = 0;
  ConfitResolution *left = 0;
  ConfitResolution *right = 0;
  size_t index;
  ConfitStatus status = confit_host_root_open_absolute(
      options->root, 0, &project_root, diagnostic);
  if (status == CONFIT_OK)
    status = confit_schema_project_load(project_root, options->project, 0,
                                        &project, diagnostic);
  if (status == CONFIT_OK)
    status = confit_cli_resolve_one(project_root, project, options->config,
                                    &left_config, &left, diagnostic);
  if (status == CONFIT_OK)
    status = confit_cli_resolve_one(project_root, project,
                                    options->other_config, &right_config,
                                    &right, diagnostic);
  if (status == CONFIT_OK &&
      confit_resolution_value_count(left) !=
          confit_resolution_value_count(right)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0U, 0U,
                          kCliInternal);
    status = CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; status == CONFIT_OK &&
                  index < confit_resolution_value_count(left);
       ++index) {
    const ConfitResolvedValue *left_value = 0;
    const ConfitResolvedValue *right_value = 0;
    char left_text[CONFIT_LIMIT_STRING_BYTES * 2U + 32U];
    char right_text[CONFIT_LIMIT_STRING_BYTES * 2U + 32U];
    if (!confit_resolution_value_at(left, index, &left_value) ||
        !confit_resolution_value_at(right, index, &right_value) ||
        left_value == 0 || right_value == 0 ||
        strcmp(left_value->symbol, right_value->symbol) != 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0U, 0U,
                            kCliInternal);
      status = CONFIT_ERR_INTERNAL;
      break;
    }
    if (confit_value_equal(&left_value->effective_value,
                           &right_value->effective_value) &&
        left_value->available == right_value->available)
      continue;
    if (!confit_cli_format_value(&left_value->effective_value, left_text,
                                 sizeof(left_text)) ||
        !confit_cli_format_value(&right_value->effective_value, right_text,
                                 sizeof(right_text)) ||
        fprintf(stdout, "%s: %s [%s] -> %s [%s]\n", left_value->symbol,
                left_text, left_value->available ? "available" : "unavailable",
                right_text,
                right_value->available ? "available" : "unavailable") < 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_IO, 0, 0U, 0U,
                            kCliOutputFailed);
      status = CONFIT_ERR_IO;
    }
  }
  confit_resolution_destroy(right);
  confit_resolution_destroy(left);
  confit_user_config_destroy(right_config);
  confit_user_config_destroy(left_config);
  confit_schema_project_destroy(project);
  confit_host_root_destroy(project_root);
  return status;
}

static ConfitStatus confit_cli_dispatch(const ConfitCliOptions *options,
                                        ConfitDiagnostic *diagnostic) {
  switch (options->spec->command) {
  case CONFIT_CLI_COMMAND_CHECK:
    return confit_cli_command_check(options, diagnostic);
  case CONFIT_CLI_COMMAND_CONFIGURE:
    return confit_cli_command_configure(options, diagnostic);
  case CONFIT_CLI_COMMAND_MENUCONFIG:
    confit_diagnostic_set(diagnostic, CONFIT_ERR_TERMINAL, 0, 0U, 0U,
                          kCliTerminalUnavailable);
    return CONFIT_ERR_TERMINAL;
  case CONFIT_CLI_COMMAND_VERIFY:
    return confit_cli_command_verify(options, diagnostic);
  case CONFIT_CLI_COMMAND_SEARCH:
    return confit_cli_command_search(options, diagnostic);
  case CONFIT_CLI_COMMAND_EXPLAIN:
    return confit_cli_command_explain(options, diagnostic);
  case CONFIT_CLI_COMMAND_DIFF:
    return confit_cli_command_diff(options, diagnostic);
  case CONFIT_CLI_COMMAND_LISTNEWCONFIG:
  case CONFIT_CLI_COMMAND_OLDCONFIG:
  case CONFIT_CLI_COMMAND_OLDDEFCONFIG:
  case CONFIT_CLI_COMMAND_SAVEDEFCONFIG:
    confit_diagnostic_set(diagnostic, CONFIT_ERR_USAGE, 0, 0U, 0U,
                          kCliMigrationUnavailable);
    return CONFIT_ERR_USAGE;
  case CONFIT_CLI_COMMAND_INVALID:
  default:
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0U, 0U,
                          kCliInvalidArguments);
    return CONFIT_ERR_INTERNAL;
  }
}

int confit_cli_run(int argc, char **argv) {
  ConfitCliOptions options;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;
  if (argc == 2 && strcmp(argv[1], "help") == 0) {
    confit_cli_print_help();
    return 0;
  }
  if (argc == 2 && strcmp(argv[1], "--version") == 0) {
    (void)printf("%s\nschema_contract=%d\n"
                 "schema_implementation=configuration-cli\n",
                 confit_version_string(), CONFIT_SCHEMA_CONTRACT_VERSION);
    return 0;
  }
  if (argc < 2) return confit_cli_usage(0, kCliInvalidArguments);
  if (!confit_cli_parse(argc, argv, &options))
    return confit_cli_usage(argv[1], kCliInvalidArguments);
  confit_diagnostic_init(&diagnostic);
  status = confit_cli_dispatch(&options, &diagnostic);
  return confit_cli_status(options.spec->name, status, &diagnostic);
}
