#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "confit/generation_v4.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <dirent.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include "../../schema/v4/config_internal.h"
#include "confit/digest.h"
#include "confit/host.h"
#include "confit/version.h"

enum {
  CONFIT_V4_GENERATION_MAX_TEXT = 16U * 1024U * 1024U,
  CONFIT_V4_GENERATION_MAX_MEMBERS = 1024U,
};

typedef struct ConfitV4Text {
  char *bytes;
  size_t size;
  size_t capacity;
} ConfitV4Text;

typedef struct ConfitV4ArtifactOwned {
  const char *name;
  char *text;
  size_t size;
  char sha256[65];
} ConfitV4ArtifactOwned;

typedef struct ConfitV4Member {
  const char *relative;
  size_t size;
  char sha256[65];
} ConfitV4Member;

struct ConfitV4GenerationTransaction {
  ConfitV4ArtifactOwned artifacts[CONFIT_V4_GENERATION_ARTIFACT_COUNT];
  char generation_sha256[65];
  char *transaction_root;
  char *generation_directory;
  char *profile_id;
  char *target_id;
  char *transaction_id;
  int active;
};

static const char *const kArtifactNames[CONFIT_V4_GENERATION_ARTIFACT_COUNT] = {
    "config.h",          "config.mk",          "selection.mk",
    "target.mk",         "config.state.toml", "config.provenance.json",
    "config.inputs.json", "config.seal.json"};

static const char *const kGenerationRoleNames[CONFIT_V4_ROLE_COUNT] = {
    "options", "menus", "choices", "constraints", "profiles",
    "targets", "selections", "products"};

static char *v4_copy(const char *text) {
  size_t size;
  char *copy;
  if (text == 0) return 0;
  size = strlen(text);
  copy = (char *)malloc(size + 1U);
  if (copy != 0) memcpy(copy, text, size + 1U);
  return copy;
}

static void set_diag(ConfitDiagnostic *diagnostic, ConfitStatus status,
                     const char *path, const char *message) {
  if (diagnostic != 0)
    confit_diagnostic_set(diagnostic, status, path, 1U, 1U, message);
}

static ConfitStatus text_reserve(ConfitV4Text *text, size_t extra) {
  size_t required;
  size_t capacity;
  char *grown;
  if (text == 0 || extra > CONFIT_V4_GENERATION_MAX_TEXT ||
      text->size > CONFIT_V4_GENERATION_MAX_TEXT - extra)
    return CONFIT_ERR_GENERATION;
  required = text->size + extra + 1U;
  if (required <= text->capacity) return CONFIT_OK;
  capacity = text->capacity == 0U ? 1024U : text->capacity;
  while (capacity < required) {
    if (capacity > CONFIT_V4_GENERATION_MAX_TEXT / 2U) {
      capacity = required;
      break;
    }
    capacity *= 2U;
  }
  grown = (char *)realloc(text->bytes, capacity);
  if (grown == 0) return CONFIT_ERR_INTERNAL;
  text->bytes = grown;
  text->capacity = capacity;
  return CONFIT_OK;
}

static ConfitStatus text_n(ConfitV4Text *text, const char *bytes, size_t size) {
  ConfitStatus status = text_reserve(text, size);
  if (status != CONFIT_OK) return status;
  if (size != 0U) memcpy(text->bytes + text->size, bytes, size);
  text->size += size;
  text->bytes[text->size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus text_s(ConfitV4Text *text, const char *bytes) {
  return text_n(text, bytes, strlen(bytes));
}

static ConfitStatus text_f(ConfitV4Text *text, const char *format, ...) {
  va_list arguments;
  va_list copy;
  int count;
  ConfitStatus status;
  va_start(arguments, format);
  va_copy(copy, arguments);
  count = vsnprintf(0, 0U, format, copy);
  va_end(copy);
  if (count < 0) {
    va_end(arguments);
    return CONFIT_ERR_INTERNAL;
  }
  status = text_reserve(text, (size_t)count);
  if (status == CONFIT_OK) {
    (void)vsnprintf(text->bytes + text->size, text->capacity - text->size,
                    format, arguments);
    text->size += (size_t)count;
  }
  va_end(arguments);
  return status;
}

static ConfitStatus json_string(ConfitV4Text *text, const char *value) {
  ConfitStatus status = text_s(text, "\"");
  if (value == 0) value = "";
  for (size_t index = 0U; status == CONFIT_OK && value[index] != '\0';
       ++index) {
    const unsigned char byte = (unsigned char)value[index];
    if (byte == '"' || byte == '\\') {
      status = text_f(text, "\\%c", (char)byte);
    } else if (byte == '\n') {
      status = text_s(text, "\\n");
    } else if (byte == '\r') {
      status = text_s(text, "\\r");
    } else if (byte == '\t') {
      status = text_s(text, "\\t");
    } else if (byte < 0x20U || byte == 0x7fU) {
      status = text_f(text, "\\u%04x", (unsigned)byte);
    } else {
      status = text_n(text, (const char *)&value[index], 1U);
    }
  }
  if (status == CONFIT_OK) status = text_s(text, "\"");
  return status;
}

static int safe_atom(const char *value) {
  size_t size;
  if (value == 0 || value[0] == '\0') return 0;
  size = strlen(value);
  if (size > 127U) return 0;
  for (size_t index = 0U; index < size; ++index) {
    const unsigned char byte = (unsigned char)value[index];
    if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
          (byte >= '0' && byte <= '9') || byte == '_' || byte == '-' ||
          byte == '.'))
      return 0;
  }
  return strcmp(value, ".") != 0 && strcmp(value, "..") != 0;
}

static int digest_text(const char *value) {
  if (value == 0 || strlen(value) != 64U) return 0;
  for (size_t index = 0U; index < 64U; ++index)
    if (!((value[index] >= '0' && value[index] <= '9') ||
          (value[index] >= 'a' && value[index] <= 'f')))
      return 0;
  return 1;
}

static int absolute_path(const char *value) {
  return value != 0 && value[0] == '/' && strstr(value, "/../") == 0 &&
         strstr(value, "/./") == 0;
}

static ConfitStatus validate_tool(const ConfitV4ToolIdentity *tool,
                                  const char *name,
                                  ConfitDiagnostic *diagnostic) {
  if (tool == 0 || !absolute_path(tool->path) || tool->version == 0 ||
      tool->version[0] == '\0' || strlen(tool->version) > 255U ||
      !digest_text(tool->sha256)) {
    set_diag(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, name,
             "tool identity needs an absolute path, bounded version, and sha256");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  return CONFIT_OK;
}

static ConfitStatus validate_current_tool(const ConfitV4ToolIdentity *tool,
                                          const char *name,
                                          ConfitDiagnostic *diagnostic) {
  char measured[65];
  ConfitStatus status = validate_tool(tool, name, diagnostic);
  if (status != CONFIT_OK) return status;
  status = confit_v4_sha256_file(tool->path, measured, diagnostic);
  if (status != CONFIT_OK || strcmp(measured, tool->sha256) != 0) {
    set_diag(diagnostic, CONFIT_ERR_COMPATIBILITY, tool->path,
             "configured tool bytes no longer match the sealed identity");
    return CONFIT_ERR_COMPATIBILITY;
  }
  return CONFIT_OK;
}

static ConfitStatus make_config_header(const ConfitV4Catalog *catalog,
                                       const ConfitV4Evaluation *evaluation,
                                       ConfitV4Text *out) {
  ConfitStatus status = text_s(
      out, "#ifndef PARUS_GENERATED_CONFIG_V4_H\n"
           "#define PARUS_GENERATED_CONFIG_V4_H\n\n"
           "/* Confit schema v4 configure-once snapshot. */\n");
  for (size_t index = 0U;
       status == CONFIT_OK && index < confit_v4_catalog_option_count(catalog);
       ++index) {
    const char *symbol = 0;
    const char *value = 0;
    int enabled = 0;
    ConfitV4SourceSpan source;
    ConfitV4OptionView option;
    if (!confit_v4_evaluation_value_at(evaluation, index, &symbol, &value,
                                       &enabled, &source) ||
        !confit_v4_catalog_option(catalog, symbol, &option))
      return CONFIT_ERR_INTERNAL;
    if (option.type == CONFIT_V4_OPTION_BOOL)
      status = text_f(out, "#define %s %d\n", option.projection,
                      enabled ? 1 : 0);
    else if (option.type == CONFIT_V4_OPTION_INTEGER)
      status = text_f(out, "#define %s %s\n", option.projection, value);
    else {
      status = text_f(out, "#define %s_ENABLED %d\n#define %s_VALUE \"",
                      option.projection, enabled ? 1 : 0, option.projection);
      if (status == CONFIT_OK) {
        for (size_t byte = 0U; value[byte] != '\0'; ++byte) {
          if (value[byte] == '"' || value[byte] == '\\')
            status = text_f(out, "\\%c", value[byte]);
          else
            status = text_n(out, value + byte, 1U);
          if (status != CONFIT_OK) break;
        }
      }
      if (status == CONFIT_OK) status = text_s(out, "\"\n");
    }
  }
  if (status == CONFIT_OK)
    status = text_s(out, "\n#endif /* PARUS_GENERATED_CONFIG_V4_H */\n");
  return status;
}

static ConfitStatus make_config_mk(const ConfitV4Evaluation *evaluation,
                                   ConfitV4Text *out) {
  ConfitStatus status = text_s(
      out, "# Confit schema v4 configure-once 값만 게시한다.\n"
           "# Source, object, link, test, generator와 GEN graph는 없다.\n");
  for (size_t index = 0U;
       status == CONFIT_OK && index < confit_v4_evaluation_value_count(evaluation);
       ++index) {
    const char *symbol = 0;
    const char *value = 0;
    int enabled = 0;
    ConfitV4SourceSpan source;
    if (!confit_v4_evaluation_value_at(evaluation, index, &symbol, &value,
                                       &enabled, &source))
      return CONFIT_ERR_INTERNAL;
    status = text_f(out, "CONFIG_%s=%s\n", symbol, value);
  }
  return status;
}

static ConfitStatus make_selection_mk(const ConfitV4Evaluation *evaluation,
                                      ConfitV4Text *out) {
  ConfitStatus status = text_s(
      out, "# Bake product-binding 입력인 option/value만 게시한다.\n"
           "# Source path, object, link order와 action은 의도적으로 금지된다.\n"
           "CONFIT_SELECTION_SCHEMA=confit-selection-v4\n");
  for (size_t index = 0U;
       status == CONFIT_OK && index < confit_v4_evaluation_value_count(evaluation);
       ++index) {
    const char *symbol = 0;
    const char *value = 0;
    int enabled = 0;
    ConfitV4SourceSpan source;
    if (!confit_v4_evaluation_value_at(evaluation, index, &symbol, &value,
                                       &enabled, &source))
      return CONFIT_ERR_INTERNAL;
    status = text_f(out, "CONFIT_SELECTION.%s=%s\n", symbol, value);
  }
  return status;
}

static ConfitStatus make_target_mk(const ConfitV4ConfigureRequest *request,
                                   ConfitV4Text *out) {
  return text_f(
      out,
      "# Target/toolchain identity만 게시하며 raw compile tuple은 없다.\n"
      "CONFIT_PROFILE=%s\nCONFIT_TARGET=%s\n"
      "CONFIT_TOOLCHAIN_PATH=%s\nCONFIT_TOOLCHAIN_VERSION=%s\n"
      "CONFIT_TOOLCHAIN_SHA256=%s\n",
      request->profile_id, request->target_id, request->toolchain.path,
      request->toolchain.version, request->toolchain.sha256);
}

static const char *option_type_name(ConfitV4OptionType type) {
  switch (type) {
  case CONFIT_V4_OPTION_BOOL: return "bool";
  case CONFIT_V4_OPTION_PLACEMENT: return "placement";
  case CONFIT_V4_OPTION_ENUM: return "enum";
  case CONFIT_V4_OPTION_INTEGER: return "integer";
  case CONFIT_V4_OPTION_STRING: return "string";
  default: return "invalid";
  }
}

static const char *reason_name(ConfitV4ReasonKind kind) {
  switch (kind) {
  case CONFIT_V4_REASON_DEFAULT: return "default";
  case CONFIT_V4_REASON_REQUEST: return "request";
  case CONFIT_V4_REASON_PREREQUISITE: return "availability";
  case CONFIT_V4_REASON_VISIBILITY: return "visibility";
  case CONFIT_V4_REASON_CHOICE: return "choice";
  case CONFIT_V4_REASON_RULE: return "rule";
  case CONFIT_V4_REASON_PROVIDER: return "provider";
  case CONFIT_V4_REASON_CYCLE: return "cycle";
  case CONFIT_V4_REASON_AMBIGUITY: return "ambiguity";
  default: return "invalid";
  }
}

static ConfitStatus make_state(const ConfitV4ConfigureRequest *request,
                              const ConfitV4Evaluation *evaluation,
                              ConfitV4Text *out) {
  ConfitStatus status = text_f(
      out,
      "schema = \"confit-config-state-v4\"\nprofile = \"%s\"\n"
      "target = \"%s\"\ntransaction = \"%s\"\n\n[values]\n",
      request->profile_id, request->target_id, request->transaction_id);
  for (size_t index = 0U;
       status == CONFIT_OK && index < confit_v4_evaluation_value_count(evaluation);
       ++index) {
    const char *symbol = 0;
    const char *value = 0;
    int enabled = 0;
    ConfitV4SourceSpan source;
    if (!confit_v4_evaluation_value_at(evaluation, index, &symbol, &value,
                                       &enabled, &source))
      return CONFIT_ERR_INTERNAL;
    status = text_f(out, "%s = \"%s\"\n", symbol, value);
  }
  return status;
}

static ConfitStatus make_provenance(const ConfitV4Catalog *catalog,
                                   const ConfitV4ConfigureRequest *request,
                                   const ConfitV4Evaluation *evaluation,
                                   ConfitV4Text *out) {
  ConfitStatus status = text_s(out, "{\"schema\":\"confit-provenance-v4\",\"options\":[");
  for (size_t index = 0U;
       status == CONFIT_OK && index < confit_v4_evaluation_value_count(evaluation);
       ++index) {
    const char *symbol = 0;
    const char *value = 0;
    int enabled = 0;
    ConfitV4SourceSpan source;
    ConfitV4OptionView option;
    if (!confit_v4_evaluation_value_at(evaluation, index, &symbol, &value,
                                       &enabled, &source) ||
        !confit_v4_catalog_option(catalog, symbol, &option))
      return CONFIT_ERR_INTERNAL;
    if (index != 0U) status = text_s(out, ",");
    if (status == CONFIT_OK) status = text_s(out, "{\"symbol\":");
    if (status == CONFIT_OK) status = json_string(out, symbol);
    if (status == CONFIT_OK) status = text_s(out, ",\"type\":");
    if (status == CONFIT_OK) status = json_string(out, option_type_name(option.type));
    if (status == CONFIT_OK) status = text_s(out, ",\"prompt\":");
    if (status == CONFIT_OK) status = json_string(out, option.prompt);
    if (status == CONFIT_OK) status = text_s(out, ",\"help\":");
    if (status == CONFIT_OK) status = json_string(out, option.help);
    if (status == CONFIT_OK) status = text_s(out, ",\"menu\":");
    if (status == CONFIT_OK) status = json_string(out, option.menu);
    if (status == CONFIT_OK)
      status = text_f(out, ",\"order\":%lld,\"owner\":",
                      (long long)option.menu_order);
    if (status == CONFIT_OK) status = json_string(out, option.owner);
    if (status == CONFIT_OK) status = text_s(out, ",\"stability\":");
    if (status == CONFIT_OK) status = json_string(out, option.stability);
    if (status == CONFIT_OK) status = text_s(out, ",\"since\":");
    if (status == CONFIT_OK) status = json_string(out, option.since);
    if (status == CONFIT_OK) status = text_s(out, ",\"tags\":[");
    for (size_t tag = 0U; status == CONFIT_OK && tag < option.tag_count; ++tag) {
      if (tag != 0U) status = text_s(out, ",");
      if (status == CONFIT_OK) status = json_string(out, option.tags[tag]);
    }
    if (status == CONFIT_OK) status = text_s(out, "],\"range\":{");
    if (status == CONFIT_OK && option.type == CONFIT_V4_OPTION_INTEGER)
      status = text_f(out, "\"minimum\":%lld,\"maximum\":%lld",
                      (long long)option.minimum, (long long)option.maximum);
    else if (status == CONFIT_OK) {
      status = text_s(out, "\"values\":[");
      for (size_t domain = 0U;
           status == CONFIT_OK && domain < option.domain_count; ++domain) {
        if (domain != 0U) status = text_s(out, ",");
        if (status == CONFIT_OK)
          status = json_string(out, option.domain_values[domain]);
      }
      if (status == CONFIT_OK) status = text_s(out, "]");
    }
    if (status == CONFIT_OK) status = text_s(out, "}");
    if (status == CONFIT_OK) status = text_s(out, ",\"default\":");
    if (status == CONFIT_OK) status = json_string(out, option.default_value);
    if (status == CONFIT_OK) status = text_s(out, ",\"requested\":");
    if (status == CONFIT_OK) status = json_string(out, value);
    if (status == CONFIT_OK) status = text_s(out, ",\"effective\":");
    if (status == CONFIT_OK) status = json_string(out, value);
    if (status == CONFIT_OK)
      status = text_f(out, ",\"enabled\":%s,\"source\":{\"path\":",
                      enabled ? "true" : "false");
    if (status == CONFIT_OK) status = json_string(out, source.path);
    if (status == CONFIT_OK)
      status = text_f(out, ",\"line\":%zu,\"column\":%zu},\"override_chain\":[",
                      source.line, source.column);
    {
      int first_override = 1;
      for (size_t assignment = 0U;
         status == CONFIT_OK && assignment < request->assignment_count;
         ++assignment) {
        const ConfitV4LayeredAssignment *layer = &request->assignments[assignment];
        if (strcmp(layer->assignment.symbol, symbol) != 0 ||
            layer->overrides_source_path == 0)
          continue;
        if (!first_override) status = text_s(out, ",");
        if (status == CONFIT_OK)
          status = json_string(out, layer->overrides_source_path);
        first_override = 0;
      }
    }
    if (status == CONFIT_OK) status = text_s(out, "]}");
  }
  if (status == CONFIT_OK) status = text_s(out, "],\"reasons\":[");
  for (size_t index = 0U;
       status == CONFIT_OK && index < confit_v4_evaluation_reason_count(evaluation);
       ++index) {
    ConfitV4ReasonView reason;
    if (!confit_v4_evaluation_reason(evaluation, index, &reason))
      return CONFIT_ERR_INTERNAL;
    if (index != 0U) status = text_s(out, ",");
    if (status == CONFIT_OK) status = text_s(out, "{\"kind\":");
    if (status == CONFIT_OK) status = json_string(out, reason_name(reason.kind));
    if (status == CONFIT_OK) status = text_s(out, ",\"subject\":");
    if (status == CONFIT_OK) status = json_string(out, reason.subject);
    if (status == CONFIT_OK) status = text_s(out, ",\"cause\":");
    if (status == CONFIT_OK) status = json_string(out, reason.cause);
    if (status == CONFIT_OK)
      status = text_f(out, ",\"satisfied\":%s,\"source\":{\"path\":",
                      reason.satisfied ? "true" : "false");
    if (status == CONFIT_OK) status = json_string(out, reason.source.path);
    if (status == CONFIT_OK)
      status = text_f(out, ",\"line\":%zu,\"column\":%zu}}",
                      reason.source.line, reason.source.column);
  }
  if (status == CONFIT_OK) status = text_s(out, "]}\n");
  return status;
}

static int relative_to_root(const char *root, const char *path,
                            const char **out_relative) {
  const size_t root_size = strlen(root);
  if (strncmp(root, path, root_size) != 0 || path[root_size] != '/') return 0;
  *out_relative = path + root_size + 1U;
  return (*out_relative)[0] != '\0';
}

static ConfitStatus collect_members(const ConfitV4Catalog *catalog,
                                    ConfitV4Member **out_members,
                                    size_t *out_count,
                                    ConfitDiagnostic *diagnostic) {
  ConfitV4Member *members;
  if (catalog->document_count > CONFIT_V4_GENERATION_MAX_MEMBERS)
    return CONFIT_ERR_GENERATION;
  members = (ConfitV4Member *)calloc(catalog->document_count, sizeof(members[0]));
  if (members == 0 && catalog->document_count != 0U)
    return CONFIT_ERR_INTERNAL;
  for (size_t index = 0U; index < catalog->document_count; ++index) {
    const char *relative = 0;
    size_t size = 0U;
    char digest[65];
    if (!relative_to_root(catalog->repository_root, catalog->documents[index],
                          &relative) ||
        confit_v4_sha256_file(catalog->documents[index], digest, diagnostic) !=
            CONFIT_OK) {
      free(members);
      return CONFIT_ERR_GENERATION;
    }
#if !defined(_WIN32)
    {
      struct stat metadata;
      if (lstat(catalog->documents[index], &metadata) != 0 ||
          S_ISLNK(metadata.st_mode) || !S_ISREG(metadata.st_mode) ||
          metadata.st_size < 0) {
        free(members);
        return CONFIT_ERR_GENERATION;
      }
      size = (size_t)metadata.st_size;
    }
#endif
    members[index].relative = relative;
    members[index].size = size;
    memcpy(members[index].sha256, digest, sizeof(digest));
  }
  *out_members = members;
  *out_count = catalog->document_count;
  return CONFIT_OK;
}

static ConfitStatus make_inputs(const ConfitV4Catalog *catalog,
                               const ConfitV4ConfigureRequest *request,
                               ConfitV4Text *out,
                               ConfitDiagnostic *diagnostic) {
  ConfitV4Member *members = 0;
  size_t member_count = 0U;
  int first_root = 1;
  ConfitStatus status = collect_members(catalog, &members, &member_count,
                                        diagnostic);
  if (status != CONFIT_OK) return status;
  status = text_s(out, "{\"schema\":\"confit-inputs-v4\",\"project\":");
  if (status == CONFIT_OK) status = json_string(out, catalog->project_name);
  if (status == CONFIT_OK) status = text_s(out, ",\"profile\":");
  if (status == CONFIT_OK) status = json_string(out, request->profile_id);
  if (status == CONFIT_OK) status = text_s(out, ",\"target\":");
  if (status == CONFIT_OK) status = json_string(out, request->target_id);
  if (status == CONFIT_OK) status = text_s(out, ",\"transaction\":");
  if (status == CONFIT_OK) status = json_string(out, request->transaction_id);
  if (status == CONFIT_OK) status = text_s(out, ",\"resolver\":[");
  if (status == CONFIT_OK) status = json_string(out, request->resolver.path);
  if (status == CONFIT_OK) status = text_s(out, ",");
  if (status == CONFIT_OK) status = json_string(out, request->resolver.version);
  if (status == CONFIT_OK) status = text_s(out, ",");
  if (status == CONFIT_OK) status = json_string(out, request->resolver.sha256);
  if (status == CONFIT_OK) status = text_s(out, "],\"toolchain\":[");
  if (status == CONFIT_OK) status = json_string(out, request->toolchain.path);
  if (status == CONFIT_OK) status = text_s(out, ",");
  if (status == CONFIT_OK) status = json_string(out, request->toolchain.version);
  if (status == CONFIT_OK) status = text_s(out, ",");
  if (status == CONFIT_OK) status = json_string(out, request->toolchain.sha256);
  if (status == CONFIT_OK) status = text_s(out, "],\"verifier\":[");
  if (status == CONFIT_OK) status = json_string(out, request->verifier.path);
  if (status == CONFIT_OK) status = text_s(out, ",");
  if (status == CONFIT_OK) status = json_string(out, request->verifier.version);
  if (status == CONFIT_OK) status = text_s(out, ",");
  if (status == CONFIT_OK) status = json_string(out, request->verifier.sha256);
  if (status == CONFIT_OK) status = text_s(out, "],\"roots\":[");
  for (size_t role = 0U; status == CONFIT_OK && role < CONFIT_V4_ROLE_COUNT;
       ++role) {
    for (size_t index = 0U;
         status == CONFIT_OK && index < catalog->roots[role].count; ++index) {
      if (!first_root) status = text_s(out, ",");
      first_root = 0;
      if (status == CONFIT_OK) status = text_s(out, "[");
      if (status == CONFIT_OK)
        status = json_string(out, kGenerationRoleNames[role]);
      if (status == CONFIT_OK) status = text_s(out, ",");
      if (status == CONFIT_OK)
        status = json_string(out, catalog->roots[role].items[index]);
      if (status == CONFIT_OK) status = text_s(out, "]");
    }
  }
  if (status == CONFIT_OK) status = text_s(out, "],\"members\":[");
  for (size_t index = 0U; status == CONFIT_OK && index < member_count; ++index) {
    if (index != 0U) status = text_s(out, ",");
    if (status == CONFIT_OK) status = text_s(out, "[");
    if (status == CONFIT_OK) status = json_string(out, members[index].relative);
    if (status == CONFIT_OK)
      status = text_f(out, ",\"file\",%zu,\"%s\"]", members[index].size,
                      members[index].sha256);
  }
  if (status == CONFIT_OK) status = text_s(out, "]}\n");
  free(members);
  return status;
}

static ConfitStatus compute_generation_digest(
    ConfitV4GenerationTransaction *transaction,
    const ConfitV4ConfigureRequest *request) {
  ConfitV4Text manifest = {0};
  ConfitStatus status = text_f(&manifest, "schema=confit-generation-v4\nprofile=%s\ntarget=%s\ntransaction=%s\n",
                               request->profile_id, request->target_id,
                               request->transaction_id);
  for (size_t index = 0U; status == CONFIT_OK && index < 7U; ++index)
    status = text_f(&manifest, "%s %zu %s\n", transaction->artifacts[index].name,
                    transaction->artifacts[index].size,
                    transaction->artifacts[index].sha256);
  if (status == CONFIT_OK)
    confit_v4_sha256_bytes(manifest.bytes, manifest.size,
                           transaction->generation_sha256);
  free(manifest.bytes);
  return status;
}

static ConfitStatus make_seal(const ConfitV4GenerationTransaction *transaction,
                             ConfitV4Text *out) {
  ConfitStatus status = text_f(
      out,
      "{\"schema\":\"confit-config-seal-v4\",\"generation_sha256\":\"%s\",\"artifacts\":[",
      transaction->generation_sha256);
  for (size_t index = 0U; status == CONFIT_OK && index < 7U; ++index) {
    if (index != 0U) status = text_s(out, ",");
    if (status == CONFIT_OK)
      status = text_f(out, "[\"%s\",%zu,\"%s\"]",
                      transaction->artifacts[index].name,
                      transaction->artifacts[index].size,
                      transaction->artifacts[index].sha256);
  }
  if (status == CONFIT_OK) status = text_s(out, "]}\n");
  return status;
}

static void transaction_free(ConfitV4GenerationTransaction *transaction) {
  if (transaction == 0) return;
  for (size_t index = 0U; index < CONFIT_V4_GENERATION_ARTIFACT_COUNT; ++index)
    free(transaction->artifacts[index].text);
  free(transaction->transaction_root);
  free(transaction->generation_directory);
  free(transaction->profile_id);
  free(transaction->target_id);
  free(transaction->transaction_id);
  free(transaction);
}

#if !defined(_WIN32)
static int directory_real(const char *path) {
  char canonical[4096];
  struct stat metadata;
  return path != 0 && realpath(path, canonical) != 0 &&
         strcmp(path, canonical) == 0 && lstat(path, &metadata) == 0 &&
         !S_ISLNK(metadata.st_mode) &&
         S_ISDIR(metadata.st_mode);
}

static int make_dir_once(const char *path, mode_t mode) {
  if (mkdir(path, mode) == 0) return 1;
  return errno == EEXIST && directory_real(path);
}

static int remove_tree_fd(int parent_fd, const char *name);

static ConfitStatus publish_candidate(
    ConfitV4GenerationTransaction *transaction,
    const ConfitV4ConfigureRequest *request, ConfitDiagnostic *diagnostic) {
  char transactions[4096];
  char transaction_path[4096];
  char generation_path[4096];
  char temporary_name[256];
  int root_fd = -1;
  int lock_fd = -1;
  int tx_fd = -1;
  int generation_fd = -1;
  int renamed = 0;
  ConfitStatus status = CONFIT_OK;
  if (snprintf(transactions, sizeof(transactions), "%s/transactions",
               request->output_root) >= (int)sizeof(transactions) ||
      snprintf(transaction_path, sizeof(transaction_path), "%s/%s",
               transactions, request->transaction_id) >=
          (int)sizeof(transaction_path) ||
      snprintf(generation_path, sizeof(generation_path), "%s/%s",
               transaction_path, transaction->generation_sha256) >=
          (int)sizeof(generation_path) ||
      snprintf(temporary_name, sizeof(temporary_name), ".preview-%s-%ld",
               request->transaction_id, (long)getpid()) >=
          (int)sizeof(temporary_name))
    return CONFIT_ERR_GENERATION;
  if (!directory_real(request->output_root) ||
      !make_dir_once(transactions, 0700)) {
    set_diag(diagnostic, CONFIT_ERR_GENERATION, request->output_root,
             "generation output root must be an existing real directory");
    return CONFIT_ERR_GENERATION;
  }
  root_fd = open(transactions, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (root_fd < 0) return CONFIT_ERR_GENERATION;
  lock_fd = openat(root_fd, ".confit-configure-v4.lock",
                   O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
  if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
    status = CONFIT_ERR_CONFLICT;
    set_diag(diagnostic, status, transactions,
             "another configure transaction owns the output root");
    goto done;
  }
  {
    struct stat existing;
    if (fstatat(root_fd, request->transaction_id, &existing,
                AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT ||
        fstatat(root_fd, temporary_name, &existing,
                AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT) {
      status = CONFIT_ERR_CONFLICT;
      set_diag(diagnostic, status, transaction_path,
               "transaction or private staging identity already exists");
      goto done;
    }
  }
  if (mkdirat(root_fd, temporary_name, 0700) != 0) {
    status = CONFIT_ERR_CONFLICT;
    set_diag(diagnostic, status, transaction_path,
             "transaction id already exists or is not create-only");
    goto done;
  }
  tx_fd = openat(root_fd, temporary_name,
                 O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (tx_fd < 0 || mkdirat(tx_fd, transaction->generation_sha256, 0700) != 0) {
    status = CONFIT_ERR_GENERATION;
    goto done;
  }
  generation_fd = openat(tx_fd, transaction->generation_sha256,
                         O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (generation_fd < 0) {
    status = CONFIT_ERR_GENERATION;
    goto done;
  }
  for (size_t index = 0U; index < CONFIT_V4_GENERATION_ARTIFACT_COUNT; ++index) {
    int file_fd = openat(generation_fd, transaction->artifacts[index].name,
                         O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0400);
    size_t offset = 0U;
    if (file_fd < 0) {
      status = CONFIT_ERR_GENERATION;
      break;
    }
    while (offset < transaction->artifacts[index].size) {
      const ssize_t written = write(file_fd,
                                    transaction->artifacts[index].text + offset,
                                    transaction->artifacts[index].size - offset);
      if (written <= 0) {
        status = CONFIT_ERR_GENERATION;
        break;
      }
      offset += (size_t)written;
    }
    if (status == CONFIT_OK && fsync(file_fd) != 0)
      status = CONFIT_ERR_GENERATION;
    if (close(file_fd) != 0 && status == CONFIT_OK)
      status = CONFIT_ERR_GENERATION;
    if (status != CONFIT_OK) break;
  }
  if (status == CONFIT_OK && fsync(generation_fd) != 0)
    status = CONFIT_ERR_GENERATION;
  if (status == CONFIT_OK && fchmod(generation_fd, 0500) != 0)
    status = CONFIT_ERR_GENERATION;
  if (status == CONFIT_OK && renameat(root_fd, temporary_name, root_fd,
                                      request->transaction_id) != 0) {
    status = CONFIT_ERR_CONFLICT;
    set_diag(diagnostic, status, transaction_path,
             "candidate transaction atomic publication failed");
  }
  if (status == CONFIT_OK) renamed = 1;
  if (status == CONFIT_OK && fsync(root_fd) != 0)
    status = CONFIT_ERR_GENERATION;
  if (status == CONFIT_OK) {
    transaction->transaction_root = v4_copy(transaction_path);
    transaction->generation_directory = v4_copy(generation_path);
    if (transaction->transaction_root == 0 ||
        transaction->generation_directory == 0)
      status = CONFIT_ERR_INTERNAL;
  }
done:
  if (generation_fd >= 0) close(generation_fd);
  if (tx_fd >= 0) close(tx_fd);
  if (status != CONFIT_OK && root_fd >= 0)
    (void)remove_tree_fd(root_fd,
                         renamed ? request->transaction_id : temporary_name);
  if (lock_fd >= 0) close(lock_fd);
  if (root_fd >= 0) close(root_fd);
  return status;
}

static int remove_tree_fd(int parent_fd, const char *name) {
  int directory_fd = openat(parent_fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  DIR *stream;
  struct dirent *entry;
  int ok = 1;
  if (directory_fd < 0) return 0;
  if (fchmod(directory_fd, 0700) != 0) {
    close(directory_fd);
    return 0;
  }
  stream = fdopendir(directory_fd);
  if (stream == 0) {
    close(directory_fd);
    return 0;
  }
  while ((entry = readdir(stream)) != 0) {
    struct stat metadata;
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    if (fstatat(directory_fd, entry->d_name, &metadata,
                AT_SYMLINK_NOFOLLOW) != 0) {
      ok = 0;
      break;
    }
    if (S_ISDIR(metadata.st_mode)) {
      if (!remove_tree_fd(directory_fd, entry->d_name)) {
        ok = 0;
        break;
      }
    } else if (unlinkat(directory_fd, entry->d_name, 0) != 0) {
      ok = 0;
      break;
    }
  }
  closedir(stream);
  return ok && unlinkat(parent_fd, name, AT_REMOVEDIR) == 0;
}
#endif

ConfitStatus confit_v4_generation_preview(
    const ConfitV4ConfigureRequest *request,
    ConfitV4GenerationTransaction **out_transaction,
    ConfitDiagnostic *diagnostic) {
  ConfitV4Catalog *catalog = 0;
  ConfitV4Evaluation *evaluation = 0;
  ConfitV4GenerationTransaction *transaction = 0;
  ConfitV4Text texts[CONFIT_V4_GENERATION_ARTIFACT_COUNT] = {{0}};
  ConfitStatus status;
  if (request == 0 || out_transaction == 0 ||
      !absolute_path(request->repository_root) ||
      !absolute_path(request->output_root) || !safe_atom(request->profile_id) ||
      !safe_atom(request->target_id) || !safe_atom(request->transaction_id))
    return CONFIT_ERR_INVALID_ARGUMENT;
  *out_transaction = 0;
  status = validate_current_tool(&request->resolver, "resolver", diagnostic);
  if (status == CONFIT_OK)
    status = validate_current_tool(&request->toolchain, "toolchain", diagnostic);
  if (status == CONFIT_OK)
    status = validate_current_tool(&request->verifier, "verifier", diagnostic);
  if (status != CONFIT_OK) return status;
  status = confit_v4_catalog_load(request->repository_root, &catalog, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_evaluate_layered(
        catalog, request->assignments, request->assignment_count,
        request->provider_choices, request->provider_choice_count, &evaluation,
        diagnostic);
  if (status != CONFIT_OK) goto done;
  transaction = (ConfitV4GenerationTransaction *)calloc(1U, sizeof(*transaction));
  if (transaction == 0) {
    status = CONFIT_ERR_INTERNAL;
    goto done;
  }
  for (size_t index = 0U; index < CONFIT_V4_GENERATION_ARTIFACT_COUNT; ++index)
    transaction->artifacts[index].name = kArtifactNames[index];
  status = make_config_header(catalog, evaluation, &texts[0]);
  if (status == CONFIT_OK) status = make_config_mk(evaluation, &texts[1]);
  if (status == CONFIT_OK) status = make_selection_mk(evaluation, &texts[2]);
  if (status == CONFIT_OK) status = make_target_mk(request, &texts[3]);
  if (status == CONFIT_OK) status = make_state(request, evaluation, &texts[4]);
  if (status == CONFIT_OK)
    status = make_provenance(catalog, request, evaluation, &texts[5]);
  if (status == CONFIT_OK)
    status = make_inputs(catalog, request, &texts[6], diagnostic);
  for (size_t index = 0U; status == CONFIT_OK && index < 7U; ++index) {
    transaction->artifacts[index].text = texts[index].bytes;
    transaction->artifacts[index].size = texts[index].size;
    texts[index].bytes = 0;
    confit_v4_sha256_bytes(transaction->artifacts[index].text,
                           transaction->artifacts[index].size,
                           transaction->artifacts[index].sha256);
  }
  if (status == CONFIT_OK)
    status = compute_generation_digest(transaction, request);
  if (status == CONFIT_OK) status = make_seal(transaction, &texts[7]);
  if (status == CONFIT_OK) {
    transaction->artifacts[7].text = texts[7].bytes;
    transaction->artifacts[7].size = texts[7].size;
    texts[7].bytes = 0;
    confit_v4_sha256_bytes(transaction->artifacts[7].text,
                           transaction->artifacts[7].size,
                           transaction->artifacts[7].sha256);
    transaction->profile_id = v4_copy(request->profile_id);
    transaction->target_id = v4_copy(request->target_id);
    transaction->transaction_id = v4_copy(request->transaction_id);
    if (transaction->profile_id == 0 || transaction->target_id == 0 ||
        transaction->transaction_id == 0)
      status = CONFIT_ERR_INTERNAL;
  }
#if defined(_WIN32)
  if (status == CONFIT_OK) status = CONFIT_ERR_UNSUPPORTED;
#else
  if (status == CONFIT_OK)
    status = publish_candidate(transaction, request, diagnostic);
#endif
  if (status == CONFIT_OK) {
    transaction->active = 1;
    *out_transaction = transaction;
    transaction = 0;
  }
done:
  for (size_t index = 0U; index < CONFIT_V4_GENERATION_ARTIFACT_COUNT; ++index)
    free(texts[index].bytes);
  transaction_free(transaction);
  confit_v4_evaluation_free(evaluation);
  confit_v4_catalog_free(catalog);
  return status;
}

ConfitStatus confit_v4_generation_cancel(
    ConfitV4GenerationTransaction **transaction_pointer,
    ConfitDiagnostic *diagnostic) {
  ConfitV4GenerationTransaction *transaction;
  if (transaction_pointer == 0 || *transaction_pointer == 0)
    return CONFIT_ERR_INVALID_ARGUMENT;
  transaction = *transaction_pointer;
#if defined(_WIN32)
  (void)diagnostic;
  return CONFIT_ERR_UNSUPPORTED;
#else
  if (transaction->active && transaction->transaction_root != 0) {
    char parent[4096];
    const char *name = strrchr(transaction->transaction_root, '/');
    int parent_fd;
    if (name == 0 || name == transaction->transaction_root ||
        (size_t)(name - transaction->transaction_root) >= sizeof(parent))
      return CONFIT_ERR_INTERNAL;
    memcpy(parent, transaction->transaction_root,
           (size_t)(name - transaction->transaction_root));
    parent[name - transaction->transaction_root] = '\0';
    parent_fd = open(parent, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (parent_fd < 0 || !remove_tree_fd(parent_fd, name + 1U)) {
      if (parent_fd >= 0) close(parent_fd);
      set_diag(diagnostic, CONFIT_ERR_GENERATION,
               transaction->transaction_root,
               "failed to cancel descriptor-owned candidate transaction");
      return CONFIT_ERR_GENERATION;
    }
    close(parent_fd);
  }
  transaction->active = 0;
  transaction_free(transaction);
  *transaction_pointer = 0;
  return CONFIT_OK;
#endif
}

const char *confit_v4_generation_digest(
    const ConfitV4GenerationTransaction *transaction) {
  return transaction != 0 ? transaction->generation_sha256 : 0;
}

int confit_v4_generation_artifact(
    const ConfitV4GenerationTransaction *transaction, size_t index,
    ConfitV4GeneratedArtifactView *out_artifact) {
  if (transaction == 0 || out_artifact == 0 ||
      index >= CONFIT_V4_GENERATION_ARTIFACT_COUNT)
    return 0;
  out_artifact->name = transaction->artifacts[index].name;
  out_artifact->text = transaction->artifacts[index].text;
  out_artifact->size = transaction->artifacts[index].size;
  out_artifact->sha256 = transaction->artifacts[index].sha256;
  return 1;
}

const char *confit_v4_generation_directory(
    const ConfitV4GenerationTransaction *transaction) {
  return transaction != 0 ? transaction->generation_directory : 0;
}

static ConfitStatus binding_digest(
    const ConfitV4ProductBindingReceipt *receipt, char output[65]) {
  ConfitV4Text text = {0};
  ConfitStatus status = text_f(&text, "schema=%s\ngeneration=%s\n",
                               receipt->schema, receipt->generation_sha256);
  for (size_t index = 0U; status == CONFIT_OK && index < receipt->binding_count;
       ++index) {
    const ConfitV4ProductBinding *binding = &receipt->bindings[index];
    if (!safe_atom(binding->symbol) || !safe_atom(binding->value) ||
        !safe_atom(binding->product_role) || binding->canonical_product == 0 ||
        binding->canonical_product[0] == '/' ||
        strstr(binding->canonical_product, "..") != 0)
      status = CONFIT_ERR_SCHEMA;
    else
      status = text_f(&text, "%s %s %s %s\n", binding->symbol,
                      binding->value, binding->product_role,
                      binding->canonical_product);
  }
  if (status == CONFIT_OK)
    confit_v4_sha256_bytes(text.bytes, text.size, output);
  free(text.bytes);
  return status;
}

ConfitStatus confit_v4_product_binding_receipt_verify(
    const ConfitV4GenerationTransaction *transaction,
    const ConfitV4ProductBindingReceipt *receipt,
    ConfitDiagnostic *diagnostic) {
  char digest[65];
  if (transaction == 0 || receipt == 0 || receipt->schema == 0 ||
      receipt->generation_sha256 == 0 || receipt->receipt_sha256 == 0 ||
      (receipt->binding_count != 0U && receipt->bindings == 0))
    return CONFIT_ERR_INVALID_ARGUMENT;
  if (strcmp(receipt->schema, "bake-product-binding-v1") != 0 ||
      strcmp(receipt->generation_sha256, transaction->generation_sha256) != 0 ||
      !digest_text(receipt->receipt_sha256) ||
      binding_digest(receipt, digest) != CONFIT_OK ||
      strcmp(digest, receipt->receipt_sha256) != 0) {
    set_diag(diagnostic, CONFIT_ERR_COMPATIBILITY, "product-binding",
             "product-binding receipt does not match the candidate generation");
    return CONFIT_ERR_COMPATIBILITY;
  }
  return CONFIT_OK;
}

ConfitStatus confit_v4_generation_apply(
    ConfitV4GenerationTransaction *transaction,
    const ConfitV4ProductBindingReceipt *receipt,
    ConfitDiagnostic *diagnostic) {
  ConfitStatus status = confit_v4_product_binding_receipt_verify(
      transaction, receipt, diagnostic);
  if (status != CONFIT_OK) return status;
  set_diag(diagnostic, CONFIT_ERR_UNSUPPORTED, "selected",
           "BPAH-R03 has no trusted Bake product-binding producer; Apply has no effect");
  return CONFIT_ERR_UNSUPPORTED;
}

typedef struct JsonCursor {
  const char *bytes;
  size_t size;
  size_t offset;
} JsonCursor;

typedef struct VerifyRoot {
  char role[32];
  char path[4096];
} VerifyRoot;

typedef struct VerifyMember {
  char path[4096];
  size_t size;
  char sha256[65];
} VerifyMember;

typedef struct VerifyInputs {
  char project[4096];
  char profile[128];
  char target[128];
  char transaction[128];
  char resolver_path[4096];
  char resolver_version[256];
  char resolver_sha256[65];
  char toolchain_path[4096];
  char toolchain_version[256];
  char toolchain_sha256[65];
  char verifier_path[4096];
  char verifier_version[256];
  char verifier_sha256[65];
  VerifyRoot roots[64];
  size_t root_count;
  VerifyMember members[CONFIT_V4_GENERATION_MAX_MEMBERS];
  size_t member_count;
} VerifyInputs;

static int json_literal(JsonCursor *cursor, const char *literal) {
  const size_t size = strlen(literal);
  if (cursor == 0 || cursor->offset > cursor->size ||
      size > cursor->size - cursor->offset ||
      memcmp(cursor->bytes + cursor->offset, literal, size) != 0)
    return 0;
  cursor->offset += size;
  return 1;
}

static int json_string_value(JsonCursor *cursor, char *out, size_t out_size) {
  size_t written = 0U;
  if (cursor == 0 || out == 0 || out_size == 0U ||
      cursor->offset >= cursor->size || cursor->bytes[cursor->offset++] != '"')
    return 0;
  while (cursor->offset < cursor->size) {
    unsigned char byte = (unsigned char)cursor->bytes[cursor->offset++];
    if (byte == '"') {
      out[written] = '\0';
      return 1;
    }
    if (byte == '\\') {
      if (cursor->offset >= cursor->size) return 0;
      byte = (unsigned char)cursor->bytes[cursor->offset++];
      switch (byte) {
      case '"': case '\\': case '/': break;
      case 'n': byte = '\n'; break;
      case 'r': byte = '\r'; break;
      case 't': byte = '\t'; break;
      default: return 0;
      }
    }
    if (byte < 0x20U || written + 1U >= out_size) return 0;
    out[written++] = (char)byte;
  }
  return 0;
}

static int json_uint(JsonCursor *cursor, size_t *out) {
  size_t value = 0U;
  size_t digits = 0U;
  if (cursor == 0 || out == 0) return 0;
  while (cursor->offset < cursor->size &&
         cursor->bytes[cursor->offset] >= '0' &&
         cursor->bytes[cursor->offset] <= '9') {
    const size_t digit = (size_t)(cursor->bytes[cursor->offset++] - '0');
    if (digits == 0U && digit == 0U && cursor->offset < cursor->size &&
        cursor->bytes[cursor->offset] >= '0' &&
        cursor->bytes[cursor->offset] <= '9')
      return 0;
    if (value > (SIZE_MAX - digit) / 10U) return 0;
    value = value * 10U + digit;
    ++digits;
  }
  if (digits == 0U) return 0;
  *out = value;
  return 1;
}

static int json_named_string(JsonCursor *cursor, const char *name, char *out,
                             size_t out_size) {
  char prefix[96];
  const int count = snprintf(prefix, sizeof(prefix), "\"%s\":", name);
  return count > 0 && (size_t)count < sizeof(prefix) &&
         json_literal(cursor, prefix) &&
         json_string_value(cursor, out, out_size);
}

static int parse_tool_array(JsonCursor *cursor, const char *name, char *path,
                            size_t path_size, char *version,
                            size_t version_size, char digest[65]) {
  char prefix[96];
  const int count = snprintf(prefix, sizeof(prefix), "\"%s\":[", name);
  return count > 0 && (size_t)count < sizeof(prefix) &&
         json_literal(cursor, prefix) &&
         json_string_value(cursor, path, path_size) &&
         json_literal(cursor, ",") &&
         json_string_value(cursor, version, version_size) &&
         json_literal(cursor, ",") &&
         json_string_value(cursor, digest, 65U) && digest_text(digest) &&
         json_literal(cursor, "]");
}

static int parse_inputs_json(const char *text, size_t size,
                             VerifyInputs *inputs) {
  JsonCursor cursor = {text, size, 0U};
  memset(inputs, 0, sizeof(*inputs));
  if (!json_literal(&cursor, "{\"schema\":\"confit-inputs-v4\","))
    return 0;
  if (!json_named_string(&cursor, "project", inputs->project,
                         sizeof(inputs->project)) ||
      !json_literal(&cursor, ",") ||
      !json_named_string(&cursor, "profile", inputs->profile,
                         sizeof(inputs->profile)) ||
      !json_literal(&cursor, ",") ||
      !json_named_string(&cursor, "target", inputs->target,
                         sizeof(inputs->target)) ||
      !json_literal(&cursor, ",") ||
      !json_named_string(&cursor, "transaction", inputs->transaction,
                         sizeof(inputs->transaction)) ||
      !json_literal(&cursor, ",") ||
      !parse_tool_array(&cursor, "resolver", inputs->resolver_path,
                        sizeof(inputs->resolver_path), inputs->resolver_version,
                        sizeof(inputs->resolver_version),
                        inputs->resolver_sha256) ||
      !json_literal(&cursor, ",") ||
      !parse_tool_array(&cursor, "toolchain", inputs->toolchain_path,
                        sizeof(inputs->toolchain_path),
                        inputs->toolchain_version,
                        sizeof(inputs->toolchain_version),
                        inputs->toolchain_sha256) ||
      !json_literal(&cursor, ",") ||
      !parse_tool_array(&cursor, "verifier", inputs->verifier_path,
                        sizeof(inputs->verifier_path), inputs->verifier_version,
                        sizeof(inputs->verifier_version),
                        inputs->verifier_sha256) ||
      !json_literal(&cursor, ",\"roots\":["))
    return 0;
  if (!json_literal(&cursor, "]")) {
    for (;;) {
      VerifyRoot *root;
      if (inputs->root_count >= sizeof(inputs->roots) / sizeof(inputs->roots[0]))
        return 0;
      root = &inputs->roots[inputs->root_count++];
      if (!json_literal(&cursor, "[") ||
          !json_string_value(&cursor, root->role, sizeof(root->role)) ||
          !json_literal(&cursor, ",") ||
          !json_string_value(&cursor, root->path, sizeof(root->path)) ||
          !json_literal(&cursor, "]"))
        return 0;
      if (json_literal(&cursor, "]")) break;
      if (!json_literal(&cursor, ",")) return 0;
    }
  }
  if (!json_literal(&cursor, ",\"members\":[")) return 0;
  if (!json_literal(&cursor, "]")) {
    for (;;) {
      VerifyMember *member;
      char type[16];
      if (inputs->member_count >= CONFIT_V4_GENERATION_MAX_MEMBERS) return 0;
      member = &inputs->members[inputs->member_count++];
      if (!json_literal(&cursor, "[") ||
          !json_string_value(&cursor, member->path, sizeof(member->path)) ||
          !json_literal(&cursor, ",") ||
          !json_string_value(&cursor, type, sizeof(type)) ||
          strcmp(type, "file") != 0 || !json_literal(&cursor, ",") ||
          !json_uint(&cursor, &member->size) || !json_literal(&cursor, ",") ||
          !json_string_value(&cursor, member->sha256,
                             sizeof(member->sha256)) ||
          !digest_text(member->sha256) || !json_literal(&cursor, "]"))
        return 0;
      if (json_literal(&cursor, "]")) break;
      if (!json_literal(&cursor, ",")) return 0;
    }
  }
  return json_literal(&cursor, "}\n") && cursor.offset == cursor.size;
}

static int parse_seal_json(const char *seal, size_t seal_size,
                           char generation[65], size_t sizes[7],
                           char digests[7][65]) {
  JsonCursor cursor = {seal, seal_size, 0U};
  if (!json_literal(&cursor,
                    "{\"schema\":\"confit-config-seal-v4\","))
    return 0;
  if (!json_named_string(&cursor, "generation_sha256", generation,
                         65U) ||
      !digest_text(generation) ||
      !json_literal(&cursor, ",\"artifacts\":["))
    return 0;
  for (size_t index = 0U; index < 7U; ++index) {
    char name[64];
    if (index != 0U && !json_literal(&cursor, ",")) return 0;
    if (!json_literal(&cursor, "[") ||
        !json_string_value(&cursor, name, sizeof(name)) ||
        strcmp(name, kArtifactNames[index]) != 0 ||
        !json_literal(&cursor, ",") || !json_uint(&cursor, &sizes[index]) ||
        !json_literal(&cursor, ",") ||
        !json_string_value(&cursor, digests[index], 65U) ||
        !digest_text(digests[index]) || !json_literal(&cursor, "]"))
      return 0;
  }
  return json_literal(&cursor, "]}\n") && cursor.offset == cursor.size;
}

static int compute_verified_generation(const VerifyInputs *inputs,
                                       const size_t sizes[7],
                                       const char digests[7][65],
                                       char output[65]) {
  ConfitV4Text manifest = {0};
  ConfitStatus status;
  if (inputs == 0 || !safe_atom(inputs->profile) ||
      !safe_atom(inputs->target) || !safe_atom(inputs->transaction))
    return 0;
  status = text_f(&manifest,
                  "schema=confit-generation-v4\nprofile=%s\ntarget=%s\n"
                  "transaction=%s\n",
                  inputs->profile, inputs->target, inputs->transaction);
  for (size_t index = 0U; status == CONFIT_OK && index < 7U; ++index)
    status = text_f(&manifest, "%s %zu %s\n", kArtifactNames[index],
                    sizes[index], digests[index]);
  if (status == CONFIT_OK)
    confit_v4_sha256_bytes(manifest.bytes, manifest.size, output);
  free(manifest.bytes);
  return status == CONFIT_OK;
}

static int compare_verify_members(const void *left, const void *right) {
  const VerifyMember *left_member = (const VerifyMember *)left;
  const VerifyMember *right_member = (const VerifyMember *)right;
  return strcmp(left_member->path, right_member->path);
}

#if !defined(_WIN32)
static int collect_current_members(const char *repository_root,
                                   const char *relative_root,
                                   VerifyMember *members, size_t *count,
                                   size_t depth) {
  char absolute[4096];
  DIR *stream;
  struct dirent *entry;
  if (depth > CONFIT_V4_MAX_DISCOVERY_DEPTH ||
      snprintf(absolute, sizeof(absolute), "%s/%s", repository_root,
               relative_root) >= (int)sizeof(absolute))
    return 0;
  if (!directory_real(absolute)) return 0;
  stream = opendir(absolute);
  if (stream == 0) return 0;
  while ((entry = readdir(stream)) != 0) {
    char child_relative[4096];
    char child_absolute[4096];
    struct stat metadata;
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    if (snprintf(child_relative, sizeof(child_relative), "%s/%s",
                 relative_root, entry->d_name) >= (int)sizeof(child_relative) ||
        snprintf(child_absolute, sizeof(child_absolute), "%s/%s",
                 repository_root, child_relative) >= (int)sizeof(child_absolute) ||
        lstat(child_absolute, &metadata) != 0 || S_ISLNK(metadata.st_mode)) {
      closedir(stream);
      return 0;
    }
    if (S_ISDIR(metadata.st_mode)) {
      if (!collect_current_members(repository_root, child_relative, members,
                                   count, depth + 1U)) {
        closedir(stream);
        return 0;
      }
    } else if (S_ISREG(metadata.st_mode) &&
               (strcmp(entry->d_name, "Config.toml") == 0 ||
                strcmp(entry->d_name, "OWNERS.toml") == 0)) {
      if (*count >= CONFIT_V4_GENERATION_MAX_MEMBERS || metadata.st_size < 0 ||
          confit_v4_sha256_file(child_absolute, members[*count].sha256, 0) !=
              CONFIT_OK) {
        closedir(stream);
        return 0;
      }
      memcpy(members[*count].path, child_relative,
             strlen(child_relative) + 1U);
      members[*count].size = (size_t)metadata.st_size;
      ++*count;
    }
  }
  return closedir(stream) == 0;
}

static int verify_exact_artifact_set(const char *generation_directory) {
  DIR *stream = opendir(generation_directory);
  struct dirent *entry;
  unsigned int seen = 0U;
  if (stream == 0) return 0;
  while ((entry = readdir(stream)) != 0) {
    size_t index;
    struct stat metadata;
    char path[4096];
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    for (index = 0U; index < CONFIT_V4_GENERATION_ARTIFACT_COUNT; ++index)
      if (strcmp(entry->d_name, kArtifactNames[index]) == 0) break;
    if (index == CONFIT_V4_GENERATION_ARTIFACT_COUNT ||
        (seen & (1U << index)) != 0U ||
        snprintf(path, sizeof(path), "%s/%s", generation_directory,
                 entry->d_name) >= (int)sizeof(path) ||
        lstat(path, &metadata) != 0 || S_ISLNK(metadata.st_mode) ||
        !S_ISREG(metadata.st_mode)) {
      closedir(stream);
      return 0;
    }
    seen |= 1U << index;
  }
  return closedir(stream) == 0 &&
         seen == ((1U << CONFIT_V4_GENERATION_ARTIFACT_COUNT) - 1U);
}
#endif

static ConfitStatus verify_inputs(const char *generation_directory,
                                  const char *repository_root,
                                  const ConfitV4ToolIdentity *toolchain,
                                  const ConfitV4ToolIdentity *verifier,
                                  ConfitDiagnostic *diagnostic) {
  char path[4096];
  char *text = 0;
  size_t size = 0U;
  VerifyInputs *inputs = 0;
  VerifyMember *current = 0;
  size_t current_count = 0U;
  char measured[65];
  inputs = (VerifyInputs *)calloc(1U, sizeof(*inputs));
  current = (VerifyMember *)calloc(CONFIT_V4_GENERATION_MAX_MEMBERS,
                                   sizeof(current[0]));
  if (inputs == 0 || current == 0) {
    free(inputs);
    free(current);
    return CONFIT_ERR_INTERNAL;
  }
  if (snprintf(path, sizeof(path), "%s/config.inputs.json",
               generation_directory) >= (int)sizeof(path) ||
      confit_host_read_text_file(path, &text, &size, diagnostic) != CONFIT_OK ||
      size > CONFIT_V4_GENERATION_MAX_TEXT ||
      !parse_inputs_json(text, size, inputs)) {
    confit_host_free(text);
    free(inputs);
    free(current);
    return CONFIT_ERR_SCHEMA;
  }
  if (strcmp(inputs->toolchain_path, toolchain->path) != 0 ||
      strcmp(inputs->toolchain_version, toolchain->version) != 0 ||
      strcmp(inputs->toolchain_sha256, toolchain->sha256) != 0 ||
      strcmp(inputs->verifier_path, verifier->path) != 0 ||
      strcmp(inputs->verifier_version, verifier->version) != 0 ||
      strcmp(inputs->verifier_sha256, verifier->sha256) != 0 ||
      confit_v4_sha256_file(toolchain->path, measured, diagnostic) != CONFIT_OK ||
      strcmp(measured, toolchain->sha256) != 0 ||
      confit_v4_sha256_file(verifier->path, measured, diagnostic) != CONFIT_OK ||
      strcmp(measured, verifier->sha256) != 0) {
    confit_host_free(text);
    free(inputs);
    free(current);
    return CONFIT_ERR_COMPATIBILITY;
  }
#if defined(_WIN32)
  confit_host_free(text);
  free(inputs);
  free(current);
  return CONFIT_ERR_UNSUPPORTED;
#else
  if (snprintf(path, sizeof(path), "%s/config/project.toml", repository_root) >=
          (int)sizeof(path) ||
      confit_v4_sha256_file(path, current[0].sha256, diagnostic) != CONFIT_OK) {
    confit_host_free(text);
    free(inputs);
    free(current);
    return CONFIT_ERR_COMPATIBILITY;
  }
  {
    struct stat metadata;
    if (lstat(path, &metadata) != 0 || S_ISLNK(metadata.st_mode) ||
        !S_ISREG(metadata.st_mode) || metadata.st_size < 0) {
      confit_host_free(text);
      free(inputs);
      free(current);
      return CONFIT_ERR_COMPATIBILITY;
    }
    memcpy(current[0].path, "config/project.toml",
           sizeof("config/project.toml"));
    current[0].size = (size_t)metadata.st_size;
    current_count = 1U;
  }
  for (size_t index = 0U; index < inputs->root_count; ++index) {
    if (!collect_current_members(repository_root, inputs->roots[index].path,
                                 current, &current_count, 0U)) {
      confit_host_free(text);
      free(inputs);
      free(current);
      return CONFIT_ERR_COMPATIBILITY;
    }
  }
  qsort(current, current_count, sizeof(current[0]), compare_verify_members);
  if (current_count != inputs->member_count) {
    confit_host_free(text);
    free(inputs);
    free(current);
    return CONFIT_ERR_COMPATIBILITY;
  }
  for (size_t index = 0U; index < current_count; ++index) {
    if (strcmp(current[index].path, inputs->members[index].path) != 0 ||
        current[index].size != inputs->members[index].size ||
        strcmp(current[index].sha256, inputs->members[index].sha256) != 0) {
      confit_host_free(text);
      free(inputs);
      free(current);
      return CONFIT_ERR_COMPATIBILITY;
    }
  }
#endif
  confit_host_free(text);
  free(inputs);
  free(current);
  return CONFIT_OK;
}

ConfitStatus confit_v4_configseal_verify(
    const char *generation_directory, const char *repository_root,
    const ConfitV4ToolIdentity *toolchain,
    const ConfitV4ToolIdentity *verifier, ConfitDiagnostic *diagnostic) {
  char path[4096];
  char *seal = 0;
  size_t seal_size = 0U;
  size_t expected_sizes[7];
  char expected_digests[7][65];
  char expected_generation[65];
  char measured_generation[65];
  const char *generation_name;
  ConfitStatus status;
  (void)repository_root;
  if (!absolute_path(generation_directory) || !absolute_path(repository_root))
    return CONFIT_ERR_INVALID_ARGUMENT;
#if !defined(_WIN32)
  if (!directory_real(generation_directory) || !directory_real(repository_root))
    return CONFIT_ERR_COMPATIBILITY;
#endif
  status = validate_tool(toolchain, "toolchain", diagnostic);
  if (status == CONFIT_OK) status = validate_tool(verifier, "verifier", diagnostic);
  if (status != CONFIT_OK) return status;
#if !defined(_WIN32)
  if (!verify_exact_artifact_set(generation_directory)) {
    set_diag(diagnostic, CONFIT_ERR_SCHEMA, generation_directory,
             "generation directory must contain exactly eight regular artifacts");
    return CONFIT_ERR_SCHEMA;
  }
#endif
  if (snprintf(path, sizeof(path), "%s/config.seal.json", generation_directory) >=
      (int)sizeof(path))
    return CONFIT_ERR_INVALID_ARGUMENT;
  status = confit_host_read_text_file(path, &seal, &seal_size, diagnostic);
  if (status != CONFIT_OK || seal_size > 65536U ||
      !parse_seal_json(seal, seal_size, expected_generation, expected_sizes,
                       expected_digests)) {
    confit_host_free(seal);
    return CONFIT_ERR_SCHEMA;
  }
  for (size_t index = 0U; index < 7U; ++index) {
    size_t measured_size = 0U;
    char measured_digest[65];
    if (snprintf(path, sizeof(path), "%s/%s", generation_directory,
                 kArtifactNames[index]) >= (int)sizeof(path) ||
        confit_v4_sha256_file(path, measured_digest, diagnostic) != CONFIT_OK) {
      confit_host_free(seal);
      return CONFIT_ERR_GENERATION;
    }
#if !defined(_WIN32)
    {
      struct stat metadata;
      if (lstat(path, &metadata) != 0 || S_ISLNK(metadata.st_mode) ||
          !S_ISREG(metadata.st_mode) || metadata.st_size < 0) {
        confit_host_free(seal);
        return CONFIT_ERR_GENERATION;
      }
      measured_size = (size_t)metadata.st_size;
    }
#endif
    if (expected_sizes[index] != measured_size ||
        strcmp(expected_digests[index], measured_digest) != 0) {
      confit_host_free(seal);
      set_diag(diagnostic, CONFIT_ERR_COMPATIBILITY, path,
               "configseal artifact size or digest mismatch");
      return CONFIT_ERR_COMPATIBILITY;
    }
  }
  status = verify_inputs(generation_directory, repository_root, toolchain,
                         verifier, diagnostic);
  if (status != CONFIT_OK) {
    confit_host_free(seal);
    return status;
  }
  {
    char inputs_path[4096];
    char *inputs_text = 0;
    size_t inputs_size = 0U;
    VerifyInputs *inputs = calloc(1U, sizeof(*inputs));
    if (inputs == 0 ||
        snprintf(inputs_path, sizeof(inputs_path), "%s/config.inputs.json",
                 generation_directory) >= (int)sizeof(inputs_path) ||
        confit_host_read_text_file(inputs_path, &inputs_text, &inputs_size,
                                   diagnostic) != CONFIT_OK ||
        !parse_inputs_json(inputs_text, inputs_size, inputs) ||
        !compute_verified_generation(inputs, expected_sizes, expected_digests,
                                     measured_generation)) {
      free(inputs);
      confit_host_free(inputs_text);
      confit_host_free(seal);
      return CONFIT_ERR_SCHEMA;
    }
    free(inputs);
    confit_host_free(inputs_text);
  }
  generation_name = strrchr(generation_directory, '/');
  if (generation_name == 0 ||
      strcmp(generation_name + 1U, expected_generation) != 0 ||
      strcmp(measured_generation, expected_generation) != 0) {
    confit_host_free(seal);
    set_diag(diagnostic, CONFIT_ERR_COMPATIBILITY, generation_directory,
             "generation digest is not bound to the artifact manifest");
    return CONFIT_ERR_COMPATIBILITY;
  }
  confit_host_free(seal);
  return CONFIT_OK;
}
