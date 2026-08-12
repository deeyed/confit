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
  CONFIT_V4_GENERATION_MAX_BINDINGS = CONFIT_V4_MAX_OPTIONS * 2U,
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
  char *output_root;
  ConfitV4ToolIdentity binding_producer;
  ConfitV4ProductBinding *expected_bindings;
  size_t expected_binding_count;
  int active;
};

static const char *const kArtifactNames[CONFIT_V4_GENERATION_ARTIFACT_COUNT] = {
    "config.h",          "config.mk",          "selection.mk",
    "target.mk",         "config.state.toml", "config.provenance.json",
    "config.inputs.json", "config.seal.json"};

static const char *const kGenerationRoleNames[CONFIT_V4_ROLE_COUNT] = {
    "options", "menus", "choices", "constraints", "profiles",
    "targets", "selections", "products"};

/*
 * OBJROOT 안에서 재생성되는 도구의 절대 경로는 실행 시점 provenance이지
 * configuration의 의미 identity가 아니다.  Generation에는 고정 role locator와
 * version/digest만 넣고, configure/verify 시점에는 caller가 넘긴 실제 path를 다시
 * 측정한다.  외부 target toolchain path는 target ABI 선택의 일부이므로 그대로
 * 봉인한다.
 */
static const char kResolverLocator[] = "@resolver";
static const char kVerifierLocator[] = "@verifier";
static const char kBindingProducerLocator[] = "@binding-producer";

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

static int optional_tool_present(const ConfitV4ToolIdentity *tool) {
  return tool != 0 &&
         (tool->path != 0 || tool->version != 0 || tool->sha256 != 0);
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
      "target = \"%s\"\n\n[values]\n",
      request->profile_id, request->target_id);
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
  if (status == CONFIT_OK) status = text_s(out, ",\"resolver\":[");
  if (status == CONFIT_OK) status = json_string(out, kResolverLocator);
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
  if (status == CONFIT_OK) status = json_string(out, kVerifierLocator);
  if (status == CONFIT_OK) status = text_s(out, ",");
  if (status == CONFIT_OK) status = json_string(out, request->verifier.version);
  if (status == CONFIT_OK) status = text_s(out, ",");
  if (status == CONFIT_OK) status = json_string(out, request->verifier.sha256);
  if (status == CONFIT_OK) status = text_s(out, "],\"binding_producer\":");
  if (status == CONFIT_OK && optional_tool_present(&request->binding_producer)) {
    status = text_s(out, "[");
    if (status == CONFIT_OK)
      status = json_string(out, kBindingProducerLocator);
    if (status == CONFIT_OK) status = text_s(out, ",");
    if (status == CONFIT_OK)
      status = json_string(out, request->binding_producer.version);
    if (status == CONFIT_OK) status = text_s(out, ",");
    if (status == CONFIT_OK)
      status = json_string(out, request->binding_producer.sha256);
    if (status == CONFIT_OK) status = text_s(out, "]");
  } else if (status == CONFIT_OK) {
    status = text_s(out, "null");
  }
  if (status == CONFIT_OK) status = text_s(out, ",\"roots\":[");
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
  ConfitStatus status = text_f(&manifest,
                               "schema=confit-generation-v4\nprofile=%s\n"
                               "target=%s\n",
                               request->profile_id, request->target_id);
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

static void binding_clear(ConfitV4ProductBinding *binding) {
  if (binding == 0) return;
  free((char *)binding->symbol);
  free((char *)binding->value);
  free((char *)binding->product_role);
  free((char *)binding->canonical_product);
  memset(binding, 0, sizeof(*binding));
}

static ConfitStatus binding_add(ConfitV4GenerationTransaction *transaction,
                                const char *symbol, const char *value,
                                const char *role, const char *base,
                                const char *suffix) {
  ConfitV4ProductBinding *binding;
  char product[CONFIT_V4_MAX_PATH_BYTES + 1U];
  if (transaction->expected_binding_count >=
          CONFIT_V4_GENERATION_MAX_BINDINGS ||
      snprintf(product, sizeof(product), "%s/%s", base, suffix) >=
          (int)sizeof(product))
    return CONFIT_ERR_GENERATION;
  binding = &transaction->expected_bindings[transaction->expected_binding_count];
  binding->symbol = v4_copy(symbol);
  binding->value = v4_copy(value);
  binding->product_role = v4_copy(role);
  binding->canonical_product = v4_copy(product);
  if (binding->symbol == 0 || binding->value == 0 ||
      binding->product_role == 0 || binding->canonical_product == 0) {
    binding_clear(binding);
    return CONFIT_ERR_INTERNAL;
  }
  ++transaction->expected_binding_count;
  return CONFIT_OK;
}

static int option_product_base(const ConfitV4Catalog *catalog,
                               const ConfitV4Option *option, char *out,
                               size_t out_size) {
  const size_t repository_size = strlen(catalog->repository_root);
  static const char suffix[] = "/Config.toml";
  for (size_t index = 0U;
       index < catalog->roots[CONFIT_V4_ROLE_PRODUCTS].count; ++index) {
    char prefix[CONFIT_V4_MAX_PATH_BYTES + 1U];
    const char *relative;
    size_t relative_size;
    if (snprintf(prefix, sizeof(prefix), "%s/%s/", catalog->repository_root,
                 catalog->roots[CONFIT_V4_ROLE_PRODUCTS].items[index]) >=
        (int)sizeof(prefix))
      return 0;
    if (strncmp(option->declaration.path, prefix, strlen(prefix)) != 0)
      continue;
    relative = option->declaration.path + repository_size + 1U;
    relative_size = strlen(relative);
    if (relative_size <= strlen(suffix) ||
        strcmp(relative + relative_size - strlen(suffix), suffix) != 0 ||
        relative_size - strlen(suffix) + 1U > out_size)
      return 0;
    memcpy(out, relative, relative_size - strlen(suffix));
    out[relative_size - strlen(suffix)] = '\0';
    return 1;
  }
  return 0;
}

static ConfitStatus collect_product_bindings(
    ConfitV4GenerationTransaction *transaction, const ConfitV4Catalog *catalog,
    const ConfitV4Evaluation *evaluation) {
  transaction->expected_bindings = (ConfitV4ProductBinding *)calloc(
      CONFIT_V4_GENERATION_MAX_BINDINGS, sizeof(ConfitV4ProductBinding));
  if (transaction->expected_bindings == 0) return CONFIT_ERR_INTERNAL;
  for (size_t index = 0U; index < catalog->option_count; ++index) {
    const ConfitV4Option *option = &catalog->options[index];
    const char *value;
    char base[CONFIT_V4_MAX_PATH_BYTES + 1U];
    ConfitStatus status;
    if (!option_product_base(catalog, option, base, sizeof(base))) continue;
    value = confit_v4_evaluation_value(evaluation, option->symbol);
    if (value == 0) return CONFIT_ERR_INTERNAL;
    if (strcmp(value, "off") == 0) continue;
    if (strcmp(value, "kernel") == 0) {
      status = binding_add(transaction, option->symbol, value, "kernel", base,
                           "kernel");
    } else if (strcmp(value, "service") == 0) {
      status = binding_add(transaction, option->symbol, value, "service", base,
                           "service");
      if (status == CONFIT_OK)
        status = binding_add(transaction, option->symbol, value, "kernel-shim",
                             base, "service/kernel");
    } else {
      status = CONFIT_ERR_SCHEMA;
    }
    if (status != CONFIT_OK) return status;
  }
  return CONFIT_OK;
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
  free(transaction->output_root);
  free((char *)transaction->binding_producer.path);
  free((char *)transaction->binding_producer.version);
  free((char *)transaction->binding_producer.sha256);
  for (size_t index = 0U; index < transaction->expected_binding_count; ++index)
    binding_clear(&transaction->expected_bindings[index]);
  free(transaction->expected_bindings);
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
  if (status == CONFIT_OK && optional_tool_present(&request->binding_producer))
    status = validate_current_tool(&request->binding_producer,
                                   "binding-producer", diagnostic);
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
  status = collect_product_bindings(transaction, catalog, evaluation);
  if (status == CONFIT_OK)
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
    transaction->output_root = v4_copy(request->output_root);
    if (optional_tool_present(&request->binding_producer)) {
      transaction->binding_producer.path =
          v4_copy(request->binding_producer.path);
      transaction->binding_producer.version =
          v4_copy(request->binding_producer.version);
      transaction->binding_producer.sha256 =
          v4_copy(request->binding_producer.sha256);
    }
    if (transaction->profile_id == 0 || transaction->target_id == 0 ||
        transaction->transaction_id == 0 || transaction->output_root == 0 ||
        (optional_tool_present(&request->binding_producer) &&
         (transaction->binding_producer.path == 0 ||
          transaction->binding_producer.version == 0 ||
          transaction->binding_producer.sha256 == 0)))
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

size_t confit_v4_generation_binding_count(
    const ConfitV4GenerationTransaction *transaction) {
  return transaction != 0 ? transaction->expected_binding_count : 0U;
}

int confit_v4_generation_binding(
    const ConfitV4GenerationTransaction *transaction, size_t index,
    ConfitV4ProductBinding *out_binding) {
  if (transaction == 0 || out_binding == 0 ||
      index >= transaction->expected_binding_count)
    return 0;
  *out_binding = transaction->expected_bindings[index];
  return 1;
}

static ConfitStatus binding_digest(
    const ConfitV4ProductBindingReceipt *receipt, char output[65]) {
  ConfitV4Text text = {0};
  ConfitStatus status = text_f(
      &text,
      "schema=%s\ngeneration=%s\nproducer.path=%s\nproducer.version=%s\n"
      "producer.sha256=%s\nbinding.count=%zu\n",
      receipt->schema, receipt->generation_sha256, receipt->producer_path,
      receipt->producer_version, receipt->producer_sha256,
      receipt->binding_count);
  for (size_t index = 0U; status == CONFIT_OK && index < receipt->binding_count;
       ++index) {
    const ConfitV4ProductBinding *binding = &receipt->bindings[index];
    if (!safe_atom(binding->symbol) || !safe_atom(binding->value) ||
        !safe_atom(binding->product_role) || binding->canonical_product == 0 ||
        binding->canonical_product[0] == '/' ||
        strstr(binding->canonical_product, "..") != 0)
      status = CONFIT_ERR_SCHEMA;
    else
      status = text_f(
          &text,
          "binding.%zu.symbol=%s\nbinding.%zu.value=%s\n"
          "binding.%zu.role=%s\nbinding.%zu.product=%s\n",
          index, binding->symbol, index, binding->value, index,
          binding->product_role, index, binding->canonical_product);
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
      receipt->producer_path == 0 || receipt->producer_version == 0 ||
      receipt->producer_sha256 == 0 ||
      (receipt->binding_count != 0U && receipt->bindings == 0))
    return CONFIT_ERR_INVALID_ARGUMENT;
  if (strcmp(receipt->schema, "bake-product-binding-v1") != 0 ||
      strcmp(receipt->generation_sha256, transaction->generation_sha256) != 0 ||
      transaction->binding_producer.path == 0 ||
      strcmp(receipt->producer_path, transaction->binding_producer.path) != 0 ||
      strcmp(receipt->producer_version,
             transaction->binding_producer.version) != 0 ||
      strcmp(receipt->producer_sha256,
             transaction->binding_producer.sha256) != 0 ||
      receipt->binding_count != transaction->expected_binding_count ||
      !digest_text(receipt->receipt_sha256) ||
      binding_digest(receipt, digest) != CONFIT_OK ||
      strcmp(digest, receipt->receipt_sha256) != 0) {
    set_diag(diagnostic, CONFIT_ERR_COMPATIBILITY, "product-binding",
             "product-binding receipt does not match the candidate generation");
    return CONFIT_ERR_COMPATIBILITY;
  }
  for (size_t index = 0U; index < receipt->binding_count; ++index) {
    const ConfitV4ProductBinding *actual = &receipt->bindings[index];
    const ConfitV4ProductBinding *expected =
        &transaction->expected_bindings[index];
    if (strcmp(actual->symbol, expected->symbol) != 0 ||
        strcmp(actual->value, expected->value) != 0 ||
        strcmp(actual->product_role, expected->product_role) != 0 ||
        strcmp(actual->canonical_product, expected->canonical_product) != 0) {
      set_diag(diagnostic, CONFIT_ERR_COMPATIBILITY, "product-binding",
               "Bake product roles differ from the configured option values");
      return CONFIT_ERR_COMPATIBILITY;
    }
  }
  return CONFIT_OK;
}

#if !defined(_WIN32)
typedef struct ParsedBindingReceipt {
  char *storage;
  ConfitV4ProductBindingReceipt view;
  ConfitV4ProductBinding bindings[CONFIT_V4_GENERATION_MAX_BINDINGS];
} ParsedBindingReceipt;

static char *receipt_value(char **cursor, const char *prefix) {
  char *line;
  char *newline;
  const size_t prefix_size = strlen(prefix);
  if (cursor == 0 || *cursor == 0 || **cursor == '\0') return 0;
  line = *cursor;
  newline = strchr(line, '\n');
  if (newline == 0 || strncmp(line, prefix, prefix_size) != 0 ||
      line[prefix_size] == '\0')
    return 0;
  *newline = '\0';
  *cursor = newline + 1U;
  return line + prefix_size;
}

static int parse_decimal_count(const char *text, size_t *out) {
  size_t value = 0U;
  if (text == 0 || text[0] == '\0') return 0;
  for (size_t index = 0U; text[index] != '\0'; ++index) {
    if (text[index] < '0' || text[index] > '9' ||
        value > (CONFIT_V4_GENERATION_MAX_BINDINGS -
                 (size_t)(text[index] - '0')) /
                    10U)
      return 0;
    value = value * 10U + (size_t)(text[index] - '0');
  }
  *out = value;
  return 1;
}

static ConfitStatus parse_binding_receipt(const char *path,
                                          ParsedBindingReceipt *parsed,
                                          ConfitDiagnostic *diagnostic) {
  char canonical[4096];
  struct stat metadata;
  char *cursor;
  char prefix[64];
  int fd = -1;
  size_t offset = 0U;
  size_t count = 0U;
  memset(parsed, 0, sizeof(*parsed));
  if (!absolute_path(path) || realpath(path, canonical) == 0 ||
      strcmp(path, canonical) != 0 ||
      (fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW)) < 0 ||
      fstat(fd, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
      metadata.st_size <= 0 || metadata.st_size > 65536) {
    if (fd >= 0) close(fd);
    set_diag(diagnostic, CONFIT_ERR_GENERATION, path,
             "Bake receipt must be one bounded canonical regular file");
    return CONFIT_ERR_GENERATION;
  }
  parsed->storage = (char *)malloc((size_t)metadata.st_size + 1U);
  if (parsed->storage == 0) {
    close(fd);
    return CONFIT_ERR_INTERNAL;
  }
  while (offset < (size_t)metadata.st_size) {
    const ssize_t got = read(fd, parsed->storage + offset,
                             (size_t)metadata.st_size - offset);
    if (got <= 0) break;
    offset += (size_t)got;
  }
  close(fd);
  if (offset != (size_t)metadata.st_size ||
      memchr(parsed->storage, '\0', offset) != 0 ||
      parsed->storage[offset - 1U] != '\n')
    goto malformed;
  parsed->storage[offset] = '\0';
  cursor = parsed->storage;
  parsed->view.schema = receipt_value(&cursor, "schema=");
  parsed->view.generation_sha256 = receipt_value(&cursor, "generation=");
  parsed->view.producer_path = receipt_value(&cursor, "producer.path=");
  parsed->view.producer_version = receipt_value(&cursor, "producer.version=");
  parsed->view.producer_sha256 = receipt_value(&cursor, "producer.sha256=");
  if (!parse_decimal_count(receipt_value(&cursor, "binding.count="), &count) ||
      count > CONFIT_V4_GENERATION_MAX_BINDINGS)
    goto malformed;
  parsed->view.bindings = parsed->bindings;
  parsed->view.binding_count = count;
  for (size_t index = 0U; index < count; ++index) {
    (void)snprintf(prefix, sizeof(prefix), "binding.%zu.symbol=", index);
    parsed->bindings[index].symbol = receipt_value(&cursor, prefix);
    (void)snprintf(prefix, sizeof(prefix), "binding.%zu.value=", index);
    parsed->bindings[index].value = receipt_value(&cursor, prefix);
    (void)snprintf(prefix, sizeof(prefix), "binding.%zu.role=", index);
    parsed->bindings[index].product_role = receipt_value(&cursor, prefix);
    (void)snprintf(prefix, sizeof(prefix), "binding.%zu.product=", index);
    parsed->bindings[index].canonical_product = receipt_value(&cursor, prefix);
    if (parsed->bindings[index].symbol == 0 ||
        parsed->bindings[index].value == 0 ||
        parsed->bindings[index].product_role == 0 ||
        parsed->bindings[index].canonical_product == 0)
      goto malformed;
  }
  parsed->view.receipt_sha256 = receipt_value(&cursor, "receipt.sha256=");
  if (parsed->view.schema == 0 || parsed->view.generation_sha256 == 0 ||
      parsed->view.producer_path == 0 || parsed->view.producer_version == 0 ||
      parsed->view.producer_sha256 == 0 ||
      parsed->view.receipt_sha256 == 0 || *cursor != '\0')
    goto malformed;
  return CONFIT_OK;
malformed:
  free(parsed->storage);
  memset(parsed, 0, sizeof(*parsed));
  set_diag(diagnostic, CONFIT_ERR_SCHEMA, path,
           "Bake receipt is malformed, reordered, duplicated, or extended");
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus verify_candidate_artifacts(
    const ConfitV4GenerationTransaction *transaction,
    ConfitDiagnostic *diagnostic) {
  DIR *stream;
  struct dirent *entry;
  size_t found = 0U;
  stream = opendir(transaction->generation_directory);
  if (stream == 0) return CONFIT_ERR_GENERATION;
  while ((entry = readdir(stream)) != 0) {
    char path[4096];
    struct stat metadata;
    char digest[65];
    size_t match = CONFIT_V4_GENERATION_ARTIFACT_COUNT;
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    for (size_t index = 0U; index < CONFIT_V4_GENERATION_ARTIFACT_COUNT;
         ++index)
      if (strcmp(entry->d_name, transaction->artifacts[index].name) == 0) {
        match = index;
        break;
      }
    if (match == CONFIT_V4_GENERATION_ARTIFACT_COUNT ||
        snprintf(path, sizeof(path), "%s/%s", transaction->generation_directory,
                 entry->d_name) >= (int)sizeof(path) ||
        lstat(path, &metadata) != 0 || !S_ISREG(metadata.st_mode) ||
        S_ISLNK(metadata.st_mode) ||
        (size_t)metadata.st_size != transaction->artifacts[match].size ||
        confit_v4_sha256_file(path, digest, diagnostic) != CONFIT_OK ||
        strcmp(digest, transaction->artifacts[match].sha256) != 0) {
      closedir(stream);
      return CONFIT_ERR_COMPATIBILITY;
    }
    ++found;
  }
  closedir(stream);
  return found == CONFIT_V4_GENERATION_ARTIFACT_COUNT ? CONFIT_OK
                                                      : CONFIT_ERR_COMPATIBILITY;
}

static ConfitStatus publish_selected_alias(
    ConfitV4GenerationTransaction *transaction, const char *receipt_path,
    const char *receipt_sha256, ConfitDiagnostic *diagnostic) {
  char temporary[128];
  char bytes[12288];
  int root_fd = -1;
  int lock_fd = -1;
  int file_fd = -1;
  int written;
  ConfitStatus status = CONFIT_OK;
  written = snprintf(bytes, sizeof(bytes),
                     "schema=confit-selected-v4\ngeneration=%s\n"
                     "transaction=%s\nreceipt.path=%s\nreceipt.sha256=%s\n"
                     "generation.path=%s\n",
                     transaction->generation_sha256,
                     transaction->transaction_id, receipt_path,
                     receipt_sha256, transaction->generation_directory);
  if (written <= 0 || written >= (int)sizeof(bytes) ||
      snprintf(temporary, sizeof(temporary), ".selected-%s-%ld",
               transaction->transaction_id, (long)getpid()) >=
          (int)sizeof(temporary))
    return CONFIT_ERR_GENERATION;
  root_fd = open(transaction->output_root,
                 O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (root_fd < 0) return CONFIT_ERR_GENERATION;
  lock_fd = openat(root_fd, ".confit-apply-v4.lock",
                   O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
    status = CONFIT_ERR_CONFLICT;
    goto done;
  }
  file_fd = openat(root_fd, temporary,
                   O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                   0400);
  if (file_fd < 0 || write(file_fd, bytes, (size_t)written) != written ||
      fsync(file_fd) != 0 || close(file_fd) != 0) {
    file_fd = -1;
    status = CONFIT_ERR_GENERATION;
    goto done;
  }
  file_fd = -1;
  if (renameat(root_fd, temporary, root_fd, "selected") != 0 ||
      fsync(root_fd) != 0)
    status = CONFIT_ERR_GENERATION;
done:
  if (file_fd >= 0) close(file_fd);
  if (status != CONFIT_OK && root_fd >= 0) (void)unlinkat(root_fd, temporary, 0);
  if (lock_fd >= 0) close(lock_fd);
  if (root_fd >= 0) close(root_fd);
  if (status != CONFIT_OK)
    set_diag(diagnostic, status, transaction->output_root,
             "selected alias atomic publication failed");
  return status;
}
#endif

ConfitStatus confit_v4_generation_apply_file(
    ConfitV4GenerationTransaction *transaction, const char *receipt_path,
    ConfitDiagnostic *diagnostic) {
#if defined(_WIN32)
  (void)transaction;
  (void)receipt_path;
  (void)diagnostic;
  return CONFIT_ERR_UNSUPPORTED;
#else
  ParsedBindingReceipt parsed;
  ConfitStatus status;
  memset(&parsed, 0, sizeof(parsed));
  if (transaction == 0 || !transaction->active || receipt_path == 0)
    return CONFIT_ERR_INVALID_ARGUMENT;
  if (transaction->binding_producer.path == 0) {
    set_diag(diagnostic, CONFIT_ERR_UNSUPPORTED, "selected",
             "candidate generation has no sealed Bake receipt producer");
    return CONFIT_ERR_UNSUPPORTED;
  }
  status = validate_current_tool(&transaction->binding_producer,
                                 "binding-producer", diagnostic);
  if (status == CONFIT_OK)
    status = parse_binding_receipt(receipt_path, &parsed, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_product_binding_receipt_verify(
        transaction, &parsed.view, diagnostic);
  if (status == CONFIT_OK)
    status = verify_candidate_artifacts(transaction, diagnostic);
  if (status == CONFIT_OK)
    status = publish_selected_alias(transaction, receipt_path,
                                    parsed.view.receipt_sha256, diagnostic);
  if (status == CONFIT_OK) transaction->active = 0;
  if (parsed.storage != 0) free(parsed.storage);
  return status;
#endif
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
  char resolver_path[4096];
  char resolver_version[256];
  char resolver_sha256[65];
  char toolchain_path[4096];
  char toolchain_version[256];
  char toolchain_sha256[65];
  char verifier_path[4096];
  char verifier_version[256];
  char verifier_sha256[65];
  char binding_producer_path[4096];
  char binding_producer_version[256];
  char binding_producer_sha256[65];
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
      !json_literal(&cursor, ",\"binding_producer\":"))
    return 0;
  if (!json_literal(&cursor, "null") &&
      (!json_literal(&cursor, "[") ||
       !json_string_value(&cursor, inputs->binding_producer_path,
                          sizeof(inputs->binding_producer_path)) ||
       !json_literal(&cursor, ",") ||
       !json_string_value(&cursor, inputs->binding_producer_version,
                          sizeof(inputs->binding_producer_version)) ||
       !json_literal(&cursor, ",") ||
       !json_string_value(&cursor, inputs->binding_producer_sha256,
                          sizeof(inputs->binding_producer_sha256)) ||
       !digest_text(inputs->binding_producer_sha256) ||
       !json_literal(&cursor, "]")))
    return 0;
  if (!json_literal(&cursor, ",\"roots\":[")) return 0;
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
      !safe_atom(inputs->target))
    return 0;
  status = text_f(&manifest,
                  "schema=confit-generation-v4\nprofile=%s\ntarget=%s\n",
                  inputs->profile, inputs->target);
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
  if (strcmp(inputs->resolver_path, kResolverLocator) != 0 ||
      strcmp(inputs->verifier_path, kVerifierLocator) != 0 ||
      (inputs->binding_producer_path[0] != '\0' &&
       strcmp(inputs->binding_producer_path, kBindingProducerLocator) != 0) ||
      strcmp(inputs->toolchain_path, toolchain->path) != 0 ||
      strcmp(inputs->toolchain_version, toolchain->version) != 0 ||
      strcmp(inputs->toolchain_sha256, toolchain->sha256) != 0 ||
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
