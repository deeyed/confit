#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif

#include "confit/generation_v5.h"

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

#include "../../schema/v5/config_internal.h"
#include "confit/digest.h"
#include "confit/host.h"

enum {
  CONFIT_V5_GENERATION_MAX_TEXT = 16U * 1024U * 1024U,
  CONFIT_V5_GENERATION_MAX_MEMBERS = 1024U,
};

typedef struct ConfitV5Text {
  char *bytes;
  size_t size;
  size_t capacity;
} ConfitV5Text;

typedef struct ConfitV5ArtifactOwned {
  const char *name;
  char *text;
  size_t size;
  char sha256[65];
} ConfitV5ArtifactOwned;

typedef struct ConfitV5Member {
  char *relative;
  size_t size;
  char sha256[65];
} ConfitV5Member;

struct ConfitV5GenerationTransaction {
  ConfitV5ArtifactOwned artifacts[CONFIT_V5_GENERATION_ARTIFACT_COUNT];
  char generation_sha256[65];
  char *transaction_root;
  char *generation_directory;
  char *transaction_id;
  char *output_root;
  int active;
  int applied;
};

static const char *const kArtifactNames[CONFIT_V5_GENERATION_ARTIFACT_COUNT] = {
    "config.h", "config.mk", "selection.json", "provenance.json",
    "input-membership.txt", "tool-identity.txt", "config.seal"};

static char *copy_text(const char *text) {
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

static ConfitStatus text_reserve(ConfitV5Text *text, size_t extra) {
  size_t required;
  size_t capacity;
  char *grown;
  if (text == 0 || extra > CONFIT_V5_GENERATION_MAX_TEXT ||
      text->size > CONFIT_V5_GENERATION_MAX_TEXT - extra)
    return CONFIT_ERR_GENERATION;
  required = text->size + extra + 1U;
  if (required <= text->capacity) return CONFIT_OK;
  capacity = text->capacity == 0U ? 1024U : text->capacity;
  while (capacity < required) {
    if (capacity > CONFIT_V5_GENERATION_MAX_TEXT / 2U) {
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

static ConfitStatus text_n(ConfitV5Text *text, const char *bytes, size_t size) {
  ConfitStatus status = text_reserve(text, size);
  if (status != CONFIT_OK) return status;
  memcpy(text->bytes + text->size, bytes, size);
  text->size += size;
  text->bytes[text->size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus text_s(ConfitV5Text *text, const char *bytes) {
  return text_n(text, bytes, strlen(bytes));
}

static ConfitStatus text_f(ConfitV5Text *text, const char *format, ...) {
  va_list arguments;
  va_list copy;
  int required;
  ConfitStatus status;
  va_start(arguments, format);
  va_copy(copy, arguments);
  required = vsnprintf(0, 0U, format, copy);
  va_end(copy);
  if (required < 0) {
    va_end(arguments);
    return CONFIT_ERR_INTERNAL;
  }
  status = text_reserve(text, (size_t)required);
  if (status == CONFIT_OK) {
    (void)vsnprintf(text->bytes + text->size,
                    text->capacity - text->size, format, arguments);
    text->size += (size_t)required;
  }
  va_end(arguments);
  return status;
}

static ConfitStatus json_string(ConfitV5Text *text, const char *value) {
  ConfitStatus status = text_s(text, "\"");
  if (value == 0) return CONFIT_ERR_INTERNAL;
  for (size_t index = 0U; status == CONFIT_OK && value[index] != '\0';
       ++index) {
    const unsigned char byte = (unsigned char)value[index];
    if (byte == '"' || byte == '\\')
      status = text_f(text, "\\%c", byte);
    else if (byte < 0x20U)
      return CONFIT_ERR_GENERATION;
    else
      status = text_n(text, value + index, 1U);
  }
  if (status == CONFIT_OK) status = text_s(text, "\"");
  return status;
}

static int safe_atom(const char *value) {
  if (value == 0 || value[0] == '\0' || strlen(value) > 127U) return 0;
  for (size_t index = 0U; value[index] != '\0'; ++index) {
    const unsigned char byte = (unsigned char)value[index];
    if (!((byte >= 'a' && byte <= 'z') ||
          (byte >= 'A' && byte <= 'Z') ||
          (byte >= '0' && byte <= '9') || byte == '-' || byte == '_'))
      return 0;
  }
  return strcmp(value, ".") != 0 && strcmp(value, "..") != 0;
}

static int digest_valid(const char *value) {
  if (value == 0 || strlen(value) != 64U) return 0;
  for (size_t index = 0U; index < 64U; ++index)
    if (!((value[index] >= '0' && value[index] <= '9') ||
          (value[index] >= 'a' && value[index] <= 'f')))
      return 0;
  return 1;
}

static int absolute_path(const char *value) {
  return value != 0 && value[0] == '/' && strlen(value) < 4096U;
}

static int relative_path(const char *value) {
  size_t component = 0U;
  if (value == 0 || value[0] == '\0' || value[0] == '/' ||
      strlen(value) >= 4096U)
    return 0;
  for (size_t index = 0U;; ++index) {
    const char byte = value[index];
    if (byte == '/' || byte == '\0') {
      if (component == 0U ||
          (component == 1U && value[index - 1U] == '.') ||
          (component == 2U && value[index - 2U] == '.' &&
           value[index - 1U] == '.'))
        return 0;
      component = 0U;
      if (byte == '\0') return 1;
    } else {
      if (byte == '\\' || byte == '\t' || byte == '\n' || byte == '\r')
        return 0;
      ++component;
    }
  }
}

static ConfitStatus validate_tool(const ConfitV5ToolIdentity *tool,
                                  const char *role,
                                  ConfitDiagnostic *diagnostic) {
  if (tool == 0 || !absolute_path(tool->path) || tool->version == 0 ||
      tool->version[0] == '\0' || strlen(tool->version) > 255U ||
      !digest_valid(tool->sha256)) {
    set_diag(diagnostic, CONFIT_ERR_INVALID_ARGUMENT,
             tool != 0 ? tool->path : 0, role);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  return CONFIT_OK;
}

static ConfitStatus validate_current_tool(const ConfitV5ToolIdentity *tool,
                                          const char *role,
                                          ConfitDiagnostic *diagnostic) {
  char measured[65];
  ConfitStatus status = validate_tool(tool, role, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v5_sha256_file(tool->path, measured, diagnostic);
  if (status != CONFIT_OK || strcmp(measured, tool->sha256) != 0) {
    set_diag(diagnostic, CONFIT_ERR_COMPATIBILITY,
             tool != 0 ? tool->path : 0,
             "configured tool bytes no longer match the sealed identity");
    return CONFIT_ERR_COMPATIBILITY;
  }
  return CONFIT_OK;
}

static ConfitStatus make_config_header(const ConfitV5Catalog *catalog,
                                       const ConfitV5Evaluation *evaluation,
                                       ConfitV5Text *out) {
  ConfitStatus status = text_s(
      out, "#ifndef LUCA_GENERATED_CONFIG_V5_H\n"
           "#define LUCA_GENERATED_CONFIG_V5_H\n\n"
           "/* Config v5 KERNCONF snapshot; generated, do not edit. */\n");
  for (size_t index = 0U;
       status == CONFIT_OK && index < confit_v5_catalog_option_count(catalog);
       ++index) {
    const char *symbol = 0;
    const char *value = 0;
    int enabled = 0;
    ConfitV5SourceSpan source;
    ConfitV5OptionView option;
    if (!confit_v5_evaluation_value_at(evaluation, index, &symbol, &value,
                                       &enabled, &source) ||
        !confit_v5_catalog_option(catalog, symbol, &option))
      return CONFIT_ERR_INTERNAL;
    if (option.type == CONFIT_V5_OPTION_BOOL)
      status = text_f(out, "#define %s %d\n", option.projection,
                      enabled ? 1 : 0);
    else if (option.type == CONFIT_V5_OPTION_INTEGER)
      status = text_f(out, "#define %s %s\n", option.projection, value);
    else {
      status = text_f(out, "#define %s_ENABLED %d\n#define %s_VALUE \"",
                      option.projection, enabled ? 1 : 0, option.projection);
      for (size_t byte = 0U; status == CONFIT_OK && value[byte] != '\0';
           ++byte) {
        if (value[byte] == '"' || value[byte] == '\\')
          status = text_f(out, "\\%c", value[byte]);
        else
          status = text_n(out, value + byte, 1U);
      }
      if (status == CONFIT_OK) status = text_s(out, "\"\n");
    }
  }
  if (status == CONFIT_OK)
    status = text_s(out, "\n#endif /* LUCA_GENERATED_CONFIG_V5_H */\n");
  return status;
}

static ConfitStatus make_config_mk(const ConfitV5Catalog *catalog,
                                   const ConfitV5Evaluation *evaluation,
                                   ConfitV5Text *out) {
  ConfitStatus status = text_s(
      out, "# Config v5 KERNCONF values only.\n"
           "# Source, link, tool and runner graphs are deliberately absent.\n");
  for (size_t index = 0U;
       status == CONFIT_OK &&
       index < confit_v5_evaluation_value_count(evaluation); ++index) {
    const char *symbol = 0;
    const char *value = 0;
    int enabled = 0;
    ConfitV5SourceSpan source;
    ConfitV5OptionView option;
    if (!confit_v5_evaluation_value_at(evaluation, index, &symbol, &value,
                                       &enabled, &source) ||
        !confit_v5_catalog_option(catalog, symbol, &option))
      return CONFIT_ERR_INTERNAL;
    if (option.type == CONFIT_V5_OPTION_STRING)
      status = text_f(
          out, "# CONFIG_%s_VALUE is available in config.h and selection.json\n",
          symbol);
    else
      status = text_f(out, "CONFIG_%s=%s\n", symbol, value);
  }
  return status;
}

static const char *option_type_name(ConfitV5OptionType type) {
  switch (type) {
  case CONFIT_V5_OPTION_BOOL: return "bool";
  case CONFIT_V5_OPTION_PLACEMENT: return "placement";
  case CONFIT_V5_OPTION_ENUM: return "enum";
  case CONFIT_V5_OPTION_INTEGER: return "integer";
  case CONFIT_V5_OPTION_STRING: return "string";
  default: return "invalid";
  }
}

static const char *reason_name(ConfitV5ReasonKind kind) {
  switch (kind) {
  case CONFIT_V5_REASON_DEFAULT: return "default";
  case CONFIT_V5_REASON_REQUEST: return "request";
  case CONFIT_V5_REASON_PREREQUISITE: return "availability";
  case CONFIT_V5_REASON_VISIBILITY: return "visibility";
  case CONFIT_V5_REASON_CHOICE: return "choice";
  case CONFIT_V5_REASON_RULE: return "rule";
  case CONFIT_V5_REASON_CYCLE: return "cycle";
  case CONFIT_V5_REASON_AMBIGUITY: return "ambiguity";
  default: return "invalid";
  }
}

static ConfitStatus make_selection(const ConfitV5Catalog *catalog,
                                   const ConfitV5Evaluation *evaluation,
                                   ConfitV5Text *out) {
  ConfitStatus status = text_s(out, "{\"schema\":\"confit-selection-v5\",\"architecture\":");
  if (status == CONFIT_OK)
    status = json_string(out, confit_v5_catalog_architecture(catalog));
  if (status == CONFIT_OK) status = text_s(out, ",\"kernconf\":");
  if (status == CONFIT_OK)
    status = json_string(out, confit_v5_catalog_kernconf(catalog));
  if (status == CONFIT_OK) status = text_s(out, ",\"values\":[");
  for (size_t index = 0U;
       status == CONFIT_OK &&
       index < confit_v5_evaluation_value_count(evaluation); ++index) {
    const char *symbol = 0;
    const char *value = 0;
    int enabled = 0;
    ConfitV5SourceSpan source;
    if (!confit_v5_evaluation_value_at(evaluation, index, &symbol, &value,
                                       &enabled, &source))
      return CONFIT_ERR_INTERNAL;
    if (index != 0U) status = text_s(out, ",");
    if (status == CONFIT_OK) status = text_s(out, "[");
    if (status == CONFIT_OK) status = json_string(out, symbol);
    if (status == CONFIT_OK) status = text_s(out, ",");
    if (status == CONFIT_OK) status = json_string(out, value);
    if (status == CONFIT_OK)
      status = text_f(out, ",%s]", enabled ? "true" : "false");
  }
  if (status == CONFIT_OK) status = text_s(out, "]}\n");
  return status;
}

static ConfitStatus make_provenance(const ConfitV5Catalog *catalog,
                                    const ConfitV5Evaluation *evaluation,
                                    ConfitV5Text *out) {
  ConfitStatus status = text_s(out, "{\"schema\":\"confit-provenance-v5\",\"options\":[");
  for (size_t index = 0U;
       status == CONFIT_OK &&
       index < confit_v5_evaluation_value_count(evaluation); ++index) {
    const char *symbol = 0;
    const char *value = 0;
    int enabled = 0;
    ConfitV5SourceSpan source;
    ConfitV5OptionView option;
    if (!confit_v5_evaluation_value_at(evaluation, index, &symbol, &value,
                                       &enabled, &source) ||
        !confit_v5_catalog_option(catalog, symbol, &option))
      return CONFIT_ERR_INTERNAL;
    if (index != 0U) status = text_s(out, ",");
    if (status == CONFIT_OK) status = text_s(out, "{\"symbol\":");
    if (status == CONFIT_OK) status = json_string(out, symbol);
    if (status == CONFIT_OK) status = text_s(out, ",\"type\":");
    if (status == CONFIT_OK) status = json_string(out, option_type_name(option.type));
    if (status == CONFIT_OK) status = text_s(out, ",\"value\":");
    if (status == CONFIT_OK) status = json_string(out, value);
    if (status == CONFIT_OK) status = text_s(out, ",\"owner\":");
    if (status == CONFIT_OK) status = json_string(out, option.owner);
    if (status == CONFIT_OK) status = text_s(out, ",\"menu\":");
    if (status == CONFIT_OK) status = json_string(out, option.menu);
    if (status == CONFIT_OK) status = text_s(out, ",\"prompt\":");
    if (status == CONFIT_OK) status = json_string(out, option.prompt);
    if (status == CONFIT_OK) status = text_s(out, ",\"help\":");
    if (status == CONFIT_OK) status = json_string(out, option.help);
    if (status == CONFIT_OK) status = text_s(out, ",\"since\":");
    if (status == CONFIT_OK) status = json_string(out, option.since);
    if (status == CONFIT_OK) status = text_s(out, ",\"stability\":");
    if (status == CONFIT_OK) status = json_string(out, option.stability);
    if (status == CONFIT_OK) status = text_s(out, ",\"source\":");
    if (status == CONFIT_OK) status = json_string(out, source.path);
    if (status == CONFIT_OK) status = text_s(out, "}");
  }
  if (status == CONFIT_OK) status = text_s(out, "],\"reasons\":[");
  for (size_t index = 0U;
       status == CONFIT_OK &&
       index < confit_v5_evaluation_reason_count(evaluation); ++index) {
    ConfitV5ReasonView reason;
    if (!confit_v5_evaluation_reason(evaluation, index, &reason))
      return CONFIT_ERR_INTERNAL;
    if (index != 0U) status = text_s(out, ",");
    if (status == CONFIT_OK) status = text_s(out, "[");
    if (status == CONFIT_OK) status = json_string(out, reason_name(reason.kind));
    if (status == CONFIT_OK) status = text_s(out, ",");
    if (status == CONFIT_OK) status = json_string(out, reason.subject);
    if (status == CONFIT_OK) status = text_s(out, ",");
    if (status == CONFIT_OK) status = json_string(out, reason.cause);
    if (status == CONFIT_OK)
      status = text_f(out, ",%s]", reason.satisfied ? "true" : "false");
  }
  if (status == CONFIT_OK) status = text_s(out, "]}\n");
  return status;
}

static int compare_members(const void *left, const void *right) {
  const ConfitV5Member *a = (const ConfitV5Member *)left;
  const ConfitV5Member *b = (const ConfitV5Member *)right;
  return strcmp(a->relative, b->relative);
}

static void members_free(ConfitV5Member *members, size_t count) {
  for (size_t index = 0U; index < count; ++index) free(members[index].relative);
  free(members);
}

static int relative_to_root(const char *root, const char *path,
                            const char **out) {
  const size_t size = strlen(root);
  if (strncmp(root, path, size) != 0 || path[size] != '/') return 0;
  *out = path + size + 1U;
  return **out != '\0';
}

static ConfitStatus collect_members(const ConfitV5Catalog *catalog,
                                    ConfitV5Member **out_members,
                                    size_t *out_count,
                                    ConfitDiagnostic *diagnostic) {
  ConfitV5Member *members;
  const size_t count = catalog->document_count;
  if (count == 0U || count > CONFIT_V5_GENERATION_MAX_MEMBERS)
    return CONFIT_ERR_GENERATION;
  members = (ConfitV5Member *)calloc(count, sizeof(members[0]));
  if (members == 0) return CONFIT_ERR_INTERNAL;
  for (size_t index = 0U; index < count; ++index) {
    const char *relative = 0;
    char *text = 0;
    size_t size = 0U;
    ConfitStatus status;
    if (!relative_to_root(catalog->repository_root,
                          catalog->documents[index], &relative)) {
      members_free(members, count);
      return CONFIT_ERR_GENERATION;
    }
    status = confit_host_read_text_file(catalog->documents[index], &text,
                                        &size, diagnostic);
    if (status != CONFIT_OK || size > CONFIT_V5_MAX_FILE_BYTES) {
      confit_host_free(text);
      members_free(members, count);
      return status != CONFIT_OK ? status : CONFIT_ERR_GENERATION;
    }
    members[index].relative = copy_text(relative);
    members[index].size = size;
    confit_v5_sha256_bytes(text, size, members[index].sha256);
    confit_host_free(text);
    if (members[index].relative == 0) {
      members_free(members, count);
      return CONFIT_ERR_INTERNAL;
    }
  }
  qsort(members, count, sizeof(members[0]), compare_members);
  *out_members = members;
  *out_count = count;
  return CONFIT_OK;
}

static ConfitStatus make_membership(const ConfitV5Catalog *catalog,
                                    ConfitV5Text *out,
                                    ConfitDiagnostic *diagnostic) {
  ConfitV5Member *members = 0;
  size_t count = 0U;
  size_t root_count = 2U;
  ConfitStatus status = collect_members(catalog, &members, &count, diagnostic);
  for (size_t role = 0U; role < CONFIT_V5_ROLE_COUNT; ++role)
    root_count += catalog->roots[role].count;
  if (status == CONFIT_OK)
    status = text_f(out,
                    "schema=confit-input-membership-v5\n"
                    "architecture=%s\nkernconf=%s\nroot_count=%zu\n",
                    catalog->architecture, catalog->kernconf, root_count);
  for (size_t role = 0U; status == CONFIT_OK && role < CONFIT_V5_ROLE_COUNT;
       ++role) {
    for (size_t index = 0U;
         status == CONFIT_OK && index < catalog->roots[role].count; ++index)
      status = text_f(out, "root=%s\n", catalog->roots[role].items[index]);
  }
  if (status == CONFIT_OK)
    status = text_f(out, "root=%s/%s\nroot=%s/%s\nmember_count=%zu\n",
                    catalog->architecture_root, catalog->architecture,
                    catalog->board_root, catalog->architecture, count);
  for (size_t index = 0U; status == CONFIT_OK && index < count; ++index)
    status = text_f(out, "member=%s\t%zu\t%s\n", members[index].relative,
                    members[index].size, members[index].sha256);
  members_free(members, count);
  return status;
}

static ConfitStatus make_tool_identity(const ConfitV5ConfigureRequest *request,
                                       ConfitV5Text *out) {
  return text_f(out,
                "schema=confit-tool-identity-v5\n"
                "resolver.version=%s\nresolver.sha256=%s\n"
                "verifier.version=%s\nverifier.sha256=%s\n",
                request->resolver.version, request->resolver.sha256,
                request->verifier.version, request->verifier.sha256);
}

static ConfitStatus compute_generation_digest(
    ConfitV5GenerationTransaction *transaction,
    const ConfitV5ConfigureRequest *request) {
  ConfitV5Text manifest = {0};
  ConfitStatus status = text_f(&manifest,
                               "schema=confit-generation-v5\n"
                               "architecture=%s\nkernconf=%s\n",
                               request->architecture, request->kernconf);
  for (size_t index = 0U; status == CONFIT_OK && index < 6U; ++index)
    status = text_f(&manifest, "%s\t%zu\t%s\n",
                    transaction->artifacts[index].name,
                    transaction->artifacts[index].size,
                    transaction->artifacts[index].sha256);
  if (status == CONFIT_OK)
    confit_v5_sha256_bytes(manifest.bytes, manifest.size,
                           transaction->generation_sha256);
  free(manifest.bytes);
  return status;
}

static ConfitStatus make_seal(const ConfitV5GenerationTransaction *transaction,
                              ConfitV5Text *out) {
  ConfitStatus status = text_f(out,
                               "schema=confit-config-seal-v5\n"
                               "generation=%s\nartifact_count=6\n",
                               transaction->generation_sha256);
  for (size_t index = 0U; status == CONFIT_OK && index < 6U; ++index)
    status = text_f(out, "artifact=%s\t%zu\t%s\n",
                    transaction->artifacts[index].name,
                    transaction->artifacts[index].size,
                    transaction->artifacts[index].sha256);
  return status;
}

static void transaction_free(ConfitV5GenerationTransaction *transaction) {
  if (transaction == 0) return;
  for (size_t index = 0U; index < CONFIT_V5_GENERATION_ARTIFACT_COUNT; ++index)
    free(transaction->artifacts[index].text);
  free(transaction->transaction_root);
  free(transaction->generation_directory);
  free(transaction->transaction_id);
  free(transaction->output_root);
  free(transaction);
}

#if !defined(_WIN32)
static int directory_real(const char *path) {
  char canonical[4096];
  struct stat metadata;
  return path != 0 && realpath(path, canonical) != 0 &&
         strcmp(path, canonical) == 0 && lstat(path, &metadata) == 0 &&
         !S_ISLNK(metadata.st_mode) && S_ISDIR(metadata.st_mode);
}

static int make_dir_once(const char *path, mode_t mode) {
  if (mkdir(path, mode) == 0) return 1;
  return errno == EEXIST && directory_real(path);
}

static int remove_tree_fd(int parent_fd, const char *name) {
  int directory_fd = openat(parent_fd, name,
                            O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
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

static ConfitStatus publish_candidate(
    ConfitV5GenerationTransaction *transaction,
    const ConfitV5ConfigureRequest *request, ConfitDiagnostic *diagnostic) {
  char transactions[4096];
  char transaction_path[4096];
  char generation_path[4096];
  int root_fd = -1;
  int tx_fd = -1;
  int generation_fd = -1;
  ConfitStatus status = CONFIT_OK;
  if (snprintf(transactions, sizeof(transactions), "%s/transactions",
               request->output_root) >= (int)sizeof(transactions) ||
      snprintf(transaction_path, sizeof(transaction_path), "%s/%s",
               transactions, request->transaction_id) >=
          (int)sizeof(transaction_path) ||
      snprintf(generation_path, sizeof(generation_path), "%s/%s",
               transaction_path, transaction->generation_sha256) >=
          (int)sizeof(generation_path))
    return CONFIT_ERR_GENERATION;
  if (!directory_real(request->output_root) ||
      !make_dir_once(transactions, 0700))
    return CONFIT_ERR_GENERATION;
  root_fd = open(transactions, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (root_fd < 0 || mkdirat(root_fd, request->transaction_id, 0700) != 0) {
    status = CONFIT_ERR_CONFLICT;
    goto done;
  }
  tx_fd = openat(root_fd, request->transaction_id,
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
  for (size_t index = 0U; index < CONFIT_V5_GENERATION_ARTIFACT_COUNT;
       ++index) {
    int file_fd = openat(generation_fd, transaction->artifacts[index].name,
                         O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0400);
    size_t offset = 0U;
    if (file_fd < 0) {
      status = CONFIT_ERR_GENERATION;
      break;
    }
    while (status == CONFIT_OK &&
           offset < transaction->artifacts[index].size) {
      const ssize_t written = write(
          file_fd, transaction->artifacts[index].text + offset,
          transaction->artifacts[index].size - offset);
      if (written <= 0)
        status = CONFIT_ERR_GENERATION;
      else
        offset += (size_t)written;
    }
    if (status == CONFIT_OK && fsync(file_fd) != 0)
      status = CONFIT_ERR_GENERATION;
    if (close(file_fd) != 0 && status == CONFIT_OK)
      status = CONFIT_ERR_GENERATION;
    if (status != CONFIT_OK) break;
  }
  if (status == CONFIT_OK) {
    transaction->transaction_root = copy_text(transaction_path);
    transaction->generation_directory = copy_text(generation_path);
    if (transaction->transaction_root == 0 ||
        transaction->generation_directory == 0)
      status = CONFIT_ERR_INTERNAL;
  }
done:
  if (generation_fd >= 0) close(generation_fd);
  if (tx_fd >= 0) close(tx_fd);
  if (status != CONFIT_OK && root_fd >= 0)
    (void)remove_tree_fd(root_fd, request->transaction_id);
  if (root_fd >= 0) close(root_fd);
  if (status != CONFIT_OK)
    set_diag(diagnostic, status, request->output_root,
             "candidate generation publication failed");
  return status;
}
#endif

ConfitStatus confit_v5_generation_preview(
    const ConfitV5ConfigureRequest *request,
    ConfitV5GenerationTransaction **out_transaction,
    ConfitDiagnostic *diagnostic) {
  ConfitV5CatalogRequest catalog_request;
  ConfitV5Catalog *catalog = 0;
  ConfitV5Evaluation *evaluation = 0;
  ConfitV5GenerationTransaction *transaction = 0;
  ConfitV5Text texts[CONFIT_V5_GENERATION_ARTIFACT_COUNT] = {{0}};
  ConfitStatus status;
  if (request == 0 || out_transaction == 0 ||
      !absolute_path(request->repository_root) ||
      !absolute_path(request->output_root) ||
      !safe_atom(request->architecture) || !safe_atom(request->kernconf) ||
      !safe_atom(request->transaction_id))
    return CONFIT_ERR_INVALID_ARGUMENT;
  *out_transaction = 0;
  status = validate_current_tool(&request->resolver, "resolver", diagnostic);
  if (status == CONFIT_OK)
    status = validate_current_tool(&request->verifier, "verifier", diagnostic);
  catalog_request.repository_root = request->repository_root;
  catalog_request.architecture = request->architecture;
  catalog_request.kernconf = request->kernconf;
  if (status == CONFIT_OK)
    status = confit_v5_catalog_load(&catalog_request, &catalog, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v5_evaluate_kernconf(catalog, &evaluation, diagnostic);
  if (status != CONFIT_OK) goto done;
  transaction = (ConfitV5GenerationTransaction *)calloc(1U,
                                                         sizeof(*transaction));
  if (transaction == 0) {
    status = CONFIT_ERR_INTERNAL;
    goto done;
  }
  for (size_t index = 0U; index < CONFIT_V5_GENERATION_ARTIFACT_COUNT; ++index)
    transaction->artifacts[index].name = kArtifactNames[index];
  status = make_config_header(catalog, evaluation, &texts[0]);
  if (status == CONFIT_OK)
    status = make_config_mk(catalog, evaluation, &texts[1]);
  if (status == CONFIT_OK) status = make_selection(catalog, evaluation, &texts[2]);
  if (status == CONFIT_OK) status = make_provenance(catalog, evaluation, &texts[3]);
  if (status == CONFIT_OK) status = make_membership(catalog, &texts[4], diagnostic);
  if (status == CONFIT_OK) status = make_tool_identity(request, &texts[5]);
  for (size_t index = 0U; status == CONFIT_OK && index < 6U; ++index) {
    transaction->artifacts[index].text = texts[index].bytes;
    transaction->artifacts[index].size = texts[index].size;
    texts[index].bytes = 0;
    confit_v5_sha256_bytes(transaction->artifacts[index].text,
                           transaction->artifacts[index].size,
                           transaction->artifacts[index].sha256);
  }
  if (status == CONFIT_OK)
    status = compute_generation_digest(transaction, request);
  if (status == CONFIT_OK) status = make_seal(transaction, &texts[6]);
  if (status == CONFIT_OK) {
    transaction->artifacts[6].text = texts[6].bytes;
    transaction->artifacts[6].size = texts[6].size;
    texts[6].bytes = 0;
    confit_v5_sha256_bytes(transaction->artifacts[6].text,
                           transaction->artifacts[6].size,
                           transaction->artifacts[6].sha256);
    transaction->transaction_id = copy_text(request->transaction_id);
    transaction->output_root = copy_text(request->output_root);
    if (transaction->transaction_id == 0 || transaction->output_root == 0)
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
  for (size_t index = 0U; index < CONFIT_V5_GENERATION_ARTIFACT_COUNT; ++index)
    free(texts[index].bytes);
  transaction_free(transaction);
  confit_v5_evaluation_free(evaluation);
  confit_v5_catalog_free(catalog);
  return status;
}

ConfitStatus confit_v5_generation_apply(
    ConfitV5GenerationTransaction *transaction,
    ConfitDiagnostic *diagnostic) {
#if defined(_WIN32)
  (void)transaction;
  (void)diagnostic;
  return CONFIT_ERR_UNSUPPORTED;
#else
  char temporary[256];
  char target[4096];
  struct stat selected_metadata;
  int output_fd;
  int selected_result;
  if (transaction == 0 || !transaction->active || transaction->applied ||
      transaction->output_root == 0 || transaction->transaction_id == 0)
    return CONFIT_ERR_INVALID_ARGUMENT;
  if (snprintf(temporary, sizeof(temporary), ".selected-%s-%ld",
               transaction->transaction_id, (long)getpid()) >=
          (int)sizeof(temporary) ||
      snprintf(target, sizeof(target), "transactions/%s/%s",
               transaction->transaction_id,
               transaction->generation_sha256) >= (int)sizeof(target))
    return CONFIT_ERR_GENERATION;
  if (!directory_real(transaction->output_root)) return CONFIT_ERR_COMPATIBILITY;
  output_fd = open(transaction->output_root,
                   O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
  if (output_fd < 0) return CONFIT_ERR_COMPATIBILITY;
  selected_result = fstatat(output_fd, "selected", &selected_metadata,
                            AT_SYMLINK_NOFOLLOW);
  if (selected_result == 0 &&
      !S_ISLNK(selected_metadata.st_mode)) {
    close(output_fd);
    set_diag(diagnostic, CONFIT_ERR_CONFLICT, transaction->output_root,
             "selected generation path is not a replaceable pointer");
    return CONFIT_ERR_CONFLICT;
  }
  if (selected_result != 0 && errno != ENOENT) {
    close(output_fd);
    return CONFIT_ERR_GENERATION;
  }
  if (symlinkat(target, output_fd, temporary) != 0 ||
      renameat(output_fd, temporary, output_fd, "selected") != 0) {
    (void)unlinkat(output_fd, temporary, 0);
    close(output_fd);
    set_diag(diagnostic, CONFIT_ERR_GENERATION, transaction->output_root,
             "selected generation publication failed");
    return CONFIT_ERR_GENERATION;
  }
  close(output_fd);
  transaction->applied = 1;
  transaction->active = 0;
  return CONFIT_OK;
#endif
}

ConfitStatus confit_v5_generation_cancel(
    ConfitV5GenerationTransaction **transaction_pointer,
    ConfitDiagnostic *diagnostic) {
  ConfitV5GenerationTransaction *transaction;
  if (transaction_pointer == 0 || *transaction_pointer == 0)
    return CONFIT_ERR_INVALID_ARGUMENT;
  transaction = *transaction_pointer;
#if !defined(_WIN32)
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
               "failed to cancel candidate generation");
      return CONFIT_ERR_GENERATION;
    }
    close(parent_fd);
  }
#else
  (void)diagnostic;
#endif
  transaction_free(transaction);
  *transaction_pointer = 0;
  return CONFIT_OK;
}

const char *confit_v5_generation_digest(
    const ConfitV5GenerationTransaction *transaction) {
  return transaction != 0 ? transaction->generation_sha256 : 0;
}

int confit_v5_generation_artifact(
    const ConfitV5GenerationTransaction *transaction, size_t index,
    ConfitV5GeneratedArtifactView *out_artifact) {
  if (transaction == 0 || out_artifact == 0 ||
      index >= CONFIT_V5_GENERATION_ARTIFACT_COUNT)
    return 0;
  out_artifact->name = transaction->artifacts[index].name;
  out_artifact->text = transaction->artifacts[index].text;
  out_artifact->size = transaction->artifacts[index].size;
  out_artifact->sha256 = transaction->artifacts[index].sha256;
  return 1;
}

const char *confit_v5_generation_directory(
    const ConfitV5GenerationTransaction *transaction) {
  return transaction != 0 ? transaction->generation_directory : 0;
}

static int parse_size(const char *text, size_t *out) {
  size_t value = 0U;
  if (text == 0 || text[0] == '\0') return 0;
  for (size_t index = 0U; text[index] != '\0'; ++index) {
    if (text[index] < '0' || text[index] > '9' ||
        value > (SIZE_MAX - (size_t)(text[index] - '0')) / 10U)
      return 0;
    value = value * 10U + (size_t)(text[index] - '0');
  }
  *out = value;
  return 1;
}

static char *line_value(char **cursor, const char *prefix) {
  char *line = *cursor;
  char *newline;
  const size_t prefix_size = strlen(prefix);
  if (line == 0 || strncmp(line, prefix, prefix_size) != 0) return 0;
  newline = strchr(line, '\n');
  if (newline == 0) return 0;
  *newline = '\0';
  *cursor = newline + 1U;
  return line + prefix_size;
}

static int verify_exact_files(const char *directory) {
#if defined(_WIN32)
  (void)directory;
  return 0;
#else
  DIR *stream = opendir(directory);
  struct dirent *entry;
  unsigned int seen = 0U;
  if (stream == 0) return 0;
  while ((entry = readdir(stream)) != 0) {
    int found = 0;
    char child[4096];
    struct stat metadata;
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    if (snprintf(child, sizeof(child), "%s/%s", directory, entry->d_name) >=
            (int)sizeof(child) ||
        lstat(child, &metadata) != 0 || S_ISLNK(metadata.st_mode) ||
        !S_ISREG(metadata.st_mode)) {
      closedir(stream);
      return 0;
    }
    for (size_t index = 0U; index < CONFIT_V5_GENERATION_ARTIFACT_COUNT;
         ++index) {
      if (strcmp(entry->d_name, kArtifactNames[index]) == 0) {
        const unsigned int bit = 1U << index;
        if ((seen & bit) != 0U) {
          closedir(stream);
          return 0;
        }
        seen |= bit;
        found = 1;
        break;
      }
    }
    if (!found) {
      closedir(stream);
      return 0;
    }
  }
  closedir(stream);
  return seen == ((1U << CONFIT_V5_GENERATION_ARTIFACT_COUNT) - 1U);
#endif
}

#if !defined(_WIN32)
static int ascii_equal_folded(const char *left, const char *right) {
  size_t index = 0U;
  while (left[index] != '\0' && right[index] != '\0') {
    unsigned char a = (unsigned char)left[index];
    unsigned char b = (unsigned char)right[index];
    if (a >= 'A' && a <= 'Z') a = (unsigned char)(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = (unsigned char)(b - 'A' + 'a');
    if (a != b) return 0;
    ++index;
  }
  return left[index] == right[index];
}

static ConfitStatus scan_membership_directory(
    const char *directory, size_t depth, size_t *entry_count,
    size_t *document_count, ConfitDiagnostic *diagnostic) {
  struct stat metadata;
  DIR *stream;
  struct dirent *entry;
  ConfitStatus status = CONFIT_OK;
  if (depth > CONFIT_V5_MAX_DISCOVERY_DEPTH ||
      lstat(directory, &metadata) != 0 || S_ISLNK(metadata.st_mode) ||
      !S_ISDIR(metadata.st_mode)) {
    set_diag(diagnostic, CONFIT_ERR_COMPATIBILITY, directory,
             "sealed Config v5 discovery root changed kind or depth");
    return CONFIT_ERR_COMPATIBILITY;
  }
  stream = opendir(directory);
  if (stream == 0) return CONFIT_ERR_COMPATIBILITY;
  while ((entry = readdir(stream)) != 0) {
    char child[4096];
    const int config_folded = ascii_equal_folded(entry->d_name, "Config.toml");
    const int owners_folded = ascii_equal_folded(entry->d_name, "OWNERS.toml");
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    if ((*entry_count)++ >= CONFIT_V5_MAX_DISCOVERY_ENTRIES ||
        snprintf(child, sizeof(child), "%s/%s", directory, entry->d_name) >=
            (int)sizeof(child) ||
        lstat(child, &metadata) != 0) {
      status = CONFIT_ERR_COMPATIBILITY;
      break;
    }
    if (S_ISLNK(metadata.st_mode)) {
      status = CONFIT_ERR_COMPATIBILITY;
      break;
    }
    if (S_ISDIR(metadata.st_mode)) {
      status = scan_membership_directory(child, depth + 1U, entry_count,
                                         document_count, diagnostic);
      if (status != CONFIT_OK) break;
    } else if (S_ISREG(metadata.st_mode)) {
      if ((config_folded && strcmp(entry->d_name, "Config.toml") != 0) ||
          (owners_folded && strcmp(entry->d_name, "OWNERS.toml") != 0)) {
        status = CONFIT_ERR_COMPATIBILITY;
        break;
      }
      if (config_folded || owners_folded) {
        if (*document_count >= CONFIT_V5_GENERATION_MAX_MEMBERS) {
          status = CONFIT_ERR_COMPATIBILITY;
          break;
        }
        ++*document_count;
      }
    }
  }
  if (closedir(stream) != 0 && status == CONFIT_OK)
    status = CONFIT_ERR_COMPATIBILITY;
  if (status != CONFIT_OK)
    set_diag(diagnostic, status, directory,
             "sealed Config v5 discovery membership changed");
  return status;
}
#endif

static ConfitStatus verify_seal(const char *generation_directory,
                                const char *expected_architecture,
                                const char *expected_kernconf,
                                ConfitDiagnostic *diagnostic) {
  char path[4096];
  char *seal = 0;
  char *cursor;
  size_t seal_size = 0U;
  char *generation;
  char *schema;
  char *artifact_count;
  char computed_generation[65];
  ConfitV5Text manifest = {0};
  ConfitStatus status;
  if (snprintf(path, sizeof(path), "%s/config.seal", generation_directory) >=
      (int)sizeof(path))
    return CONFIT_ERR_INVALID_ARGUMENT;
  status = confit_host_read_text_file(path, &seal, &seal_size, diagnostic);
  if (status != CONFIT_OK || seal_size > CONFIT_V5_MAX_FILE_BYTES)
    goto done;
  cursor = seal;
  schema = line_value(&cursor, "schema=");
  generation = line_value(&cursor, "generation=");
  artifact_count = line_value(&cursor, "artifact_count=");
  if (schema == 0 || strcmp(schema, "confit-config-seal-v5") != 0 ||
      generation == 0 || !digest_valid(generation) ||
      artifact_count == 0 || strcmp(artifact_count, "6") != 0) {
    status = CONFIT_ERR_COMPATIBILITY;
    goto done;
  }
  status = text_s(&manifest, "schema=confit-generation-v5\n");
  /* architecture/kernconf are recovered from selection below. */
  {
    char selection_path[4096];
    char *selection = 0;
    size_t selection_size = 0U;
    const char *architecture;
    const char *kernconf;
    if (snprintf(selection_path, sizeof(selection_path), "%s/selection.json",
                 generation_directory) >= (int)sizeof(selection_path) ||
        confit_host_read_text_file(selection_path, &selection, &selection_size,
                                   diagnostic) != CONFIT_OK) {
      status = CONFIT_ERR_COMPATIBILITY;
    } else {
      architecture = strstr(selection, "\"architecture\":\"");
      kernconf = strstr(selection, "\"kernconf\":\"");
      if (architecture == 0 || kernconf == 0) {
        status = CONFIT_ERR_COMPATIBILITY;
      } else {
        char arch[128];
        char conf[128];
        size_t ai = 0U, ki = 0U;
        architecture += strlen("\"architecture\":\"");
        kernconf += strlen("\"kernconf\":\"");
        while (architecture[ai] != '\0' && architecture[ai] != '"' &&
               ai + 1U < sizeof(arch)) {
          arch[ai] = architecture[ai];
          ++ai;
        }
        while (kernconf[ki] != '\0' && kernconf[ki] != '"' &&
               ki + 1U < sizeof(conf)) {
          conf[ki] = kernconf[ki];
          ++ki;
        }
        arch[ai] = '\0';
        conf[ki] = '\0';
        if (!safe_atom(arch) || !safe_atom(conf) ||
            strcmp(arch, expected_architecture) != 0 ||
            strcmp(conf, expected_kernconf) != 0)
          status = CONFIT_ERR_COMPATIBILITY;
        else
          status = text_f(&manifest, "architecture=%s\nkernconf=%s\n",
                          arch, conf);
      }
    }
    confit_host_free(selection);
  }
  for (size_t index = 0U; status == CONFIT_OK && index < 6U; ++index) {
    char *line = line_value(&cursor, "artifact=");
    char *tab1;
    char *tab2;
    char artifact_path[4096];
    char measured[65];
    char *text = 0;
    size_t size = 0U;
    size_t declared_size;
    if (line == 0 || (tab1 = strchr(line, '\t')) == 0 ||
        (tab2 = strchr(tab1 + 1U, '\t')) == 0) {
      status = CONFIT_ERR_COMPATIBILITY;
      break;
    }
    *tab1 = '\0';
    *tab2 = '\0';
    if (strcmp(line, kArtifactNames[index]) != 0 ||
        !parse_size(tab1 + 1U, &declared_size) ||
        !digest_valid(tab2 + 1U) ||
        snprintf(artifact_path, sizeof(artifact_path), "%s/%s",
                 generation_directory, line) >= (int)sizeof(artifact_path) ||
        confit_host_read_text_file(artifact_path, &text, &size, diagnostic) !=
            CONFIT_OK) {
      status = CONFIT_ERR_COMPATIBILITY;
      confit_host_free(text);
      break;
    }
    confit_v5_sha256_bytes(text, size, measured);
    confit_host_free(text);
    if (size != declared_size || strcmp(measured, tab2 + 1U) != 0) {
      status = CONFIT_ERR_COMPATIBILITY;
      break;
    }
    status = text_f(&manifest, "%s\t%zu\t%s\n", line, size, measured);
  }
  if (status == CONFIT_OK && cursor[0] != '\0') status = CONFIT_ERR_COMPATIBILITY;
  if (status == CONFIT_OK) {
    confit_v5_sha256_bytes(manifest.bytes, manifest.size, computed_generation);
    if (strcmp(computed_generation, generation) != 0)
      status = CONFIT_ERR_COMPATIBILITY;
  }
done:
  free(manifest.bytes);
  confit_host_free(seal);
  if (status != CONFIT_OK)
    set_diag(diagnostic, status, path, "configuration seal is malformed or stale");
  return status;
}

static ConfitStatus verify_tool_identity(
    const char *generation_directory, const ConfitV5ToolIdentity *verifier,
    ConfitDiagnostic *diagnostic) {
  char path[4096];
  char *text = 0;
  char *cursor;
  size_t size = 0U;
  char *schema;
  char *resolver_version;
  char *resolver_digest;
  char *verifier_version;
  char *verifier_digest;
  ConfitStatus status = validate_current_tool(verifier, "verifier", diagnostic);
  if (status != CONFIT_OK) return status;
  if (snprintf(path, sizeof(path), "%s/tool-identity.txt",
               generation_directory) >= (int)sizeof(path))
    return CONFIT_ERR_INVALID_ARGUMENT;
  status = confit_host_read_text_file(path, &text, &size, diagnostic);
  if (status != CONFIT_OK || size > CONFIT_V5_MAX_FILE_BYTES) goto done;
  cursor = text;
  schema = line_value(&cursor, "schema=");
  resolver_version = line_value(&cursor, "resolver.version=");
  resolver_digest = line_value(&cursor, "resolver.sha256=");
  verifier_version = line_value(&cursor, "verifier.version=");
  verifier_digest = line_value(&cursor, "verifier.sha256=");
  if (schema == 0 || strcmp(schema, "confit-tool-identity-v5") != 0 ||
      resolver_version == 0 || resolver_version[0] == '\0' ||
      !digest_valid(resolver_digest) || verifier_version == 0 ||
      strcmp(verifier_version, verifier->version) != 0 ||
      !digest_valid(verifier_digest) ||
      strcmp(verifier_digest, verifier->sha256) != 0 || cursor[0] != '\0')
    status = CONFIT_ERR_COMPATIBILITY;
done:
  confit_host_free(text);
  if (status != CONFIT_OK)
    set_diag(diagnostic, status, path, "tool identity is malformed or stale");
  return status;
}

static ConfitStatus verify_membership(const char *generation_directory,
                                      const char *repository_root,
                                      ConfitDiagnostic *diagnostic) {
  char path[4096];
  char *text = 0;
  char *cursor;
  size_t text_size = 0U;
  char *schema;
  char *architecture;
  char *kernconf;
  char *count_text;
  size_t root_count;
  size_t member_count;
  size_t scanned_entries = 0U;
  size_t scanned_documents = 0U;
  ConfitStatus status;
  if (snprintf(path, sizeof(path), "%s/input-membership.txt",
               generation_directory) >= (int)sizeof(path))
    return CONFIT_ERR_INVALID_ARGUMENT;
  status = confit_host_read_text_file(path, &text, &text_size, diagnostic);
  if (status != CONFIT_OK || text_size > CONFIT_V5_GENERATION_MAX_TEXT)
    goto done;
  cursor = text;
  schema = line_value(&cursor, "schema=");
  architecture = line_value(&cursor, "architecture=");
  kernconf = line_value(&cursor, "kernconf=");
  count_text = line_value(&cursor, "root_count=");
  if (schema == 0 || strcmp(schema, "confit-input-membership-v5") != 0 ||
      !safe_atom(architecture) || !safe_atom(kernconf) ||
      !parse_size(count_text, &root_count) || root_count == 0U ||
      root_count > 128U) {
    status = CONFIT_ERR_COMPATIBILITY;
    goto done;
  }
  /* The verifier re-walks only the sealed discovery roots.  It never parses
   * option semantics or Make/source files, but additions and removals must
   * invalidate the immutable configure snapshot. */
  for (size_t index = 0U; index < root_count; ++index) {
    char *root = line_value(&cursor, "root=");
    char root_path[4096];
    if (!relative_path(root) ||
        snprintf(root_path, sizeof(root_path), "%s/%s", repository_root,
                 root) >= (int)sizeof(root_path)) {
      status = CONFIT_ERR_COMPATIBILITY;
      goto done;
    }
#if !defined(_WIN32)
    status = scan_membership_directory(root_path, 0U, &scanned_entries,
                                       &scanned_documents, diagnostic);
    if (status != CONFIT_OK) goto done;
#endif
  }
  count_text = line_value(&cursor, "member_count=");
  if (!parse_size(count_text, &member_count) || member_count == 0U ||
      member_count > CONFIT_V5_GENERATION_MAX_MEMBERS) {
    status = CONFIT_ERR_COMPATIBILITY;
    goto done;
  }
  for (size_t index = 0U; index < member_count; ++index) {
    char *line = line_value(&cursor, "member=");
    char *tab1;
    char *tab2;
    char member_path[4096];
    char measured[65];
    size_t expected_size;
    char *member_text = 0;
    size_t member_size = 0U;
#if !defined(_WIN32)
    struct stat metadata;
#endif
    if (line == 0 || (tab1 = strchr(line, '\t')) == 0 ||
        (tab2 = strchr(tab1 + 1U, '\t')) == 0) {
      status = CONFIT_ERR_COMPATIBILITY;
      goto done;
    }
    *tab1 = '\0';
    *tab2 = '\0';
    if (!relative_path(line) ||
        !parse_size(tab1 + 1U, &expected_size) ||
        !digest_valid(tab2 + 1U) ||
        snprintf(member_path, sizeof(member_path), "%s/%s", repository_root,
                 line) >= (int)sizeof(member_path)
#if !defined(_WIN32)
        || lstat(member_path, &metadata) != 0 || S_ISLNK(metadata.st_mode) ||
        !S_ISREG(metadata.st_mode)
#endif
        ||
        confit_host_read_text_file(member_path, &member_text, &member_size,
                                   diagnostic) != CONFIT_OK) {
      confit_host_free(member_text);
      status = CONFIT_ERR_COMPATIBILITY;
      goto done;
    }
    confit_v5_sha256_bytes(member_text, member_size, measured);
    confit_host_free(member_text);
    if (member_size != expected_size || strcmp(measured, tab2 + 1U) != 0) {
      status = CONFIT_ERR_COMPATIBILITY;
      goto done;
    }
  }
  if (cursor[0] != '\0' || scanned_documents + 2U != member_count)
    status = CONFIT_ERR_COMPATIBILITY;
done:
  confit_host_free(text);
  if (status != CONFIT_OK)
    set_diag(diagnostic, status, path,
             "input membership is malformed or source bytes changed");
  return status;
}

ConfitStatus confit_v5_configseal_verify(
    const char *generation_directory, const char *repository_root,
    const char *expected_architecture, const char *expected_kernconf,
    const ConfitV5ToolIdentity *verifier, ConfitDiagnostic *diagnostic) {
#if defined(_WIN32)
  (void)generation_directory;
  (void)repository_root;
  (void)expected_architecture;
  (void)expected_kernconf;
  (void)verifier;
  (void)diagnostic;
  return CONFIT_ERR_UNSUPPORTED;
#else
  struct stat metadata;
  ConfitStatus status;
  if (!absolute_path(generation_directory) || !absolute_path(repository_root) ||
      !safe_atom(expected_architecture) || !safe_atom(expected_kernconf) ||
      lstat(generation_directory, &metadata) != 0 ||
      S_ISLNK(metadata.st_mode) || !S_ISDIR(metadata.st_mode) ||
      !verify_exact_files(generation_directory))
    return CONFIT_ERR_COMPATIBILITY;
  status = verify_seal(generation_directory, expected_architecture,
                       expected_kernconf, diagnostic);
  if (status == CONFIT_OK)
    status = verify_tool_identity(generation_directory, verifier, diagnostic);
  if (status == CONFIT_OK)
    status = verify_membership(generation_directory, repository_root,
                               diagnostic);
  return status;
#endif
}
