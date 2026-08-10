#include "confit/source_catalog.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/host.h"

#define CONFIT_SOURCE_MAX_BYTES 131072U
#define CONFIT_SOURCE_MAX_LINE 4096U
#define CONFIT_SOURCE_MAX_ITEMS 256U
#define CONFIT_SOURCE_MAX_DEPTH 32U
#define CONFIT_SOURCE_MAX_UNITS 256U
#define CONFIT_SOURCE_MAX_EDGES 2048U
#define CONFIT_SOURCE_MAX_TESTS 256U
#define CONFIT_SOURCE_MAX_ATOM 127U

typedef struct ConfitRestrictedMake {
  int api;
  char *include_name;
  char *unit;
  char **subdirs;
  size_t subdir_count;
  char **sources;
  size_t source_count;
  char **uses;
  size_t use_count;
  char **kapi_imports;
  size_t kapi_import_count;
  char **kapi_exports;
  size_t kapi_export_count;
  char **public_headers;
  size_t public_header_count;
  char *test_id;
  char *test_owner;
  char *test_lane;
  char *test_evidence;
  char *test_target;
  char *test_machine;
  char *test_receipt;
  uint32_t test_timeout_ms;
  int has_timeout;
} ConfitRestrictedMake;

static char *source_strdup(const char *text) {
  const size_t size = strlen(text) + 1U;
  char *copy = (char *)malloc(size);
  if (copy != NULL) memcpy(copy, text, size);
  return copy;
}

static int source_utf8(const unsigned char *text, size_t size) {
  size_t index = 0U;
  while (index < size) {
    const unsigned char first = text[index++];
    uint32_t codepoint;
    size_t continuation;
    size_t remaining;
    if (first < 0x80U) continue;
    if (first >= 0xc2U && first <= 0xdfU) {
      codepoint = (uint32_t)(first & 0x1fU);
      continuation = 1U;
    } else if (first >= 0xe0U && first <= 0xefU) {
      codepoint = (uint32_t)(first & 0x0fU);
      continuation = 2U;
    } else if (first >= 0xf0U && first <= 0xf4U) {
      codepoint = (uint32_t)(first & 0x07U);
      continuation = 3U;
    } else {
      return 0;
    }
    remaining = continuation;
    if (size - index < remaining) return 0;
    while (remaining-- != 0U) {
      const unsigned char next = text[index++];
      if ((next & 0xc0U) != 0x80U) return 0;
      codepoint = (codepoint << 6U) | (uint32_t)(next & 0x3fU);
    }
    if ((continuation == 1U && codepoint < 0x80U) ||
        (continuation == 2U && codepoint < 0x800U) ||
        (continuation == 3U && codepoint < 0x10000U) ||
        codepoint > 0x10ffffU ||
        (codepoint >= 0xd800U && codepoint <= 0xdfffU))
      return 0;
  }
  return 1;
}

static int source_path_within(const char *root, const char *path) {
  const size_t size = strlen(root);
  return strncmp(root, path, size) == 0 &&
         (path[size] == '\0' || path[size] == '/');
}

static int source_atom(const char *text, int feature) {
  size_t index;
  size_t segment = 0U;
  if (text == NULL || text[0] == '\0' || strlen(text) > CONFIT_SOURCE_MAX_ATOM)
    return 0;
  for (index = 0U; text[index] != '\0'; ++index) {
    const unsigned char value = (unsigned char)text[index];
    if ((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
        value == '-' || value == '_') {
      ++segment;
    } else if (value == '.' || (feature && value == '@')) {
      if (segment == 0U) return 0;
      segment = 0U;
    } else {
      return 0;
    }
  }
  return segment != 0U;
}

static int source_relative(const char *text, int directory) {
  size_t index;
  size_t component = 0U;
  if (text == NULL || text[0] == '\0' || text[0] == '/' ||
      strlen(text) > 1023U)
    return 0;
  for (index = 0U; text[index] != '\0'; ++index) {
    const unsigned char value = (unsigned char)text[index];
    if (value <= 0x20U || value == 0x7fU || value == '$' || value == '\\' ||
        value == '*' || value == '?' || value == '[' || value == ']')
      return 0;
    if (value == '/') {
      if (component == 0U ||
          (component == 1U && text[index - 1U] == '.') ||
          (component == 2U && text[index - 1U] == '.' &&
           text[index - 2U] == '.'))
        return 0;
      component = 0U;
    } else {
      ++component;
    }
  }
  if (component == 0U || (component == 1U && text[index - 1U] == '.') ||
      (component == 2U && text[index - 1U] == '.' && text[index - 2U] == '.'))
    return 0;
  if (directory) return 1;
  {
    const size_t size = strlen(text);
    return (size >= 2U && strcmp(text + size - 2U, ".c") == 0) ||
           (size >= 2U && strcmp(text + size - 2U, ".S") == 0) ||
           (size >= 2U && strcmp(text + size - 2U, ".s") == 0) ||
           (size >= 2U && strcmp(text + size - 2U, ".h") == 0);
  }
}

static void source_list_clear(char **items, size_t count) {
  size_t index;
  for (index = 0U; index < count; ++index) free(items[index]);
  free(items);
}

static void restricted_make_clear(ConfitRestrictedMake *make) {
  if (make == NULL) return;
  free(make->include_name);
  free(make->unit);
  source_list_clear(make->subdirs, make->subdir_count);
  source_list_clear(make->sources, make->source_count);
  source_list_clear(make->uses, make->use_count);
  source_list_clear(make->kapi_imports, make->kapi_import_count);
  source_list_clear(make->kapi_exports, make->kapi_export_count);
  source_list_clear(make->public_headers, make->public_header_count);
  free(make->test_id);
  free(make->test_owner);
  free(make->test_lane);
  free(make->test_evidence);
  free(make->test_target);
  free(make->test_machine);
  free(make->test_receipt);
  memset(make, 0, sizeof(*make));
}

static ConfitStatus source_error(ConfitDiagnostic *diagnostic,
                                 const char *path, size_t line,
                                 const char *message) {
  confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path, line, 1U, message);
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus source_list_append(char ***items, size_t *count,
                                       const char *value, const char *path,
                                       size_t line,
                                       ConfitDiagnostic *diagnostic) {
  char **grown;
  size_t index;
  if (*count >= CONFIT_SOURCE_MAX_ITEMS)
    return source_error(diagnostic, path, line,
                        "restricted Make list exceeds the supported bound");
  for (index = 0U; index < *count; ++index) {
    if (strcmp((*items)[index], value) == 0)
      return source_error(diagnostic, path, line,
                          "restricted Make list contains a duplicate token");
  }
  grown = (char **)realloc(*items, (*count + 1U) * sizeof(*grown));
  if (grown == NULL) return CONFIT_ERR_INTERNAL;
  *items = grown;
  (*items)[*count] = source_strdup(value);
  if ((*items)[*count] == NULL) return CONFIT_ERR_INTERNAL;
  ++*count;
  return CONFIT_OK;
}

static char *source_trim(char *text) {
  char *end;
  while (*text == ' ') ++text;
  end = text + strlen(text);
  while (end > text && end[-1] == ' ') --end;
  *end = '\0';
  return text;
}

static ConfitStatus source_parse_list(char *value, char ***items,
                                      size_t *count, int atom, int directory,
                                      const char *path, size_t line,
                                      ConfitDiagnostic *diagnostic) {
  char *cursor = value;
  if (cursor[0] == '\0' || cursor[0] == ' ' ||
      cursor[strlen(cursor) - 1U] == ' ')
    return source_error(diagnostic, path, line,
                        "restricted Make list has non-canonical spacing");
  while (*cursor != '\0') {
    char *space = strchr(cursor, ' ');
    ConfitStatus status;
    if (space != NULL) *space = '\0';
    if ((atom && !source_atom(cursor, 1)) ||
        (!atom && !source_relative(cursor, directory))) {
      if (space != NULL) *space = ' ';
      return source_error(diagnostic, path, line,
                          "restricted Make list contains an unsafe token");
    }
    status = source_list_append(items, count, cursor, path, line, diagnostic);
    if (space != NULL) *space = ' ';
    if (status != CONFIT_OK) return status;
    if (space == NULL) break;
    cursor = space + 1U;
    if (*cursor == ' ') return source_error(
        diagnostic, path, line,
        "restricted Make list has non-canonical spacing");
  }
  return CONFIT_OK;
}

static ConfitStatus source_assign_once(char **out, const char *value,
                                       int feature, const char *path,
                                       size_t line,
                                       ConfitDiagnostic *diagnostic) {
  if (*out != NULL || !source_atom(value, feature))
    return source_error(diagnostic, path, line,
                        "restricted Make scalar is duplicate or invalid");
  *out = source_strdup(value);
  return *out != NULL ? CONFIT_OK : CONFIT_ERR_INTERNAL;
}

static ConfitStatus source_parse_statement(
    char *statement, const char *path, size_t line, ConfitRestrictedMake *make,
    int *seen_include, ConfitDiagnostic *diagnostic) {
  static const char api[] = "PARUS_MK_API = ";
  static const char kern_unit[] = "KERN_UNIT = ";
  static const char kern_subdirs[] = "KERN_SUBDIRS += ";
  static const char sources[] = "SRCS += ";
  static const char kern_uses[] = "KERN_USES += ";
  static const char kapi_imports[] = "KAPI_IMPORTS += ";
  static const char kapi_exports[] = "KAPI_EXPORTS += ";
  static const char public_headers[] = "PUBLIC_HEADERS += ";
  static const char test_id[] = "TEST_ID = ";
  static const char test_owner[] = "TEST_OWNER = ";
  static const char test_lane[] = "TEST_LANE = ";
  static const char test_evidence[] = "TEST_EVIDENCE_CLASS = ";
  static const char test_timeout[] = "TEST_TIMEOUT_MS = ";
  static const char test_target[] = "TEST_TARGET = ";
  static const char test_machine[] = "TEST_MACHINE_PROFILE = ";
  static const char test_receipt[] = "TEST_RECEIPT_PROFILE = ";
  static const char test_sources[] = "TEST_SRCS += ";
  static const char include_prefix[] = ".include <";
  const size_t size = strlen(statement);
  char *value;
  if (statement[0] == '\0' || statement[0] == '#') return CONFIT_OK;
  if (*seen_include || strchr(statement, '\t') != NULL ||
      strchr(statement, '$') != NULL || strchr(statement, ':') != NULL ||
      strchr(statement, '!') != NULL || strchr(statement, '?') != NULL ||
      strchr(statement, '*') != NULL || strchr(statement, '[') != NULL ||
      strchr(statement, ']') != NULL || strchr(statement, '#') != NULL)
    return source_error(diagnostic, path, line,
                        "restricted Makefile contains forbidden syntax");
  if (strncmp(statement, api, sizeof(api) - 1U) == 0) {
    if (make->api != 0 || strcmp(statement + sizeof(api) - 1U, "3") != 0)
      return source_error(diagnostic, path, line,
                          "restricted Makefile requires exact API 3");
    make->api = 3;
    return CONFIT_OK;
  }
  if (strncmp(statement, kern_unit, sizeof(kern_unit) - 1U) == 0)
    return source_assign_once(&make->unit,
                              statement + sizeof(kern_unit) - 1U, 0, path,
                              line, diagnostic);
  if (strncmp(statement, kern_subdirs, sizeof(kern_subdirs) - 1U) == 0)
    return source_parse_list(statement + sizeof(kern_subdirs) - 1U,
                             &make->subdirs, &make->subdir_count, 0, 1, path,
                             line, diagnostic);
  if (strncmp(statement, sources, sizeof(sources) - 1U) == 0)
    return source_parse_list(statement + sizeof(sources) - 1U, &make->sources,
                             &make->source_count, 0, 0, path, line, diagnostic);
  if (strncmp(statement, kern_uses, sizeof(kern_uses) - 1U) == 0)
    return source_parse_list(statement + sizeof(kern_uses) - 1U, &make->uses,
                             &make->use_count, 1, 0, path, line, diagnostic);
  if (strncmp(statement, kapi_imports, sizeof(kapi_imports) - 1U) == 0)
    return source_parse_list(statement + sizeof(kapi_imports) - 1U,
                             &make->kapi_imports, &make->kapi_import_count, 1,
                             0, path, line, diagnostic);
  if (strncmp(statement, kapi_exports, sizeof(kapi_exports) - 1U) == 0)
    return source_parse_list(statement + sizeof(kapi_exports) - 1U,
                             &make->kapi_exports, &make->kapi_export_count, 1,
                             0, path, line, diagnostic);
  if (strncmp(statement, public_headers, sizeof(public_headers) - 1U) == 0)
    return source_parse_list(statement + sizeof(public_headers) - 1U,
                             &make->public_headers,
                             &make->public_header_count, 0, 0, path, line,
                             diagnostic);
  if (strncmp(statement, test_id, sizeof(test_id) - 1U) == 0)
    return source_assign_once(&make->test_id,
                              statement + sizeof(test_id) - 1U, 0, path, line,
                              diagnostic);
  if (strncmp(statement, test_owner, sizeof(test_owner) - 1U) == 0)
    return source_assign_once(&make->test_owner,
                              statement + sizeof(test_owner) - 1U, 0, path,
                              line, diagnostic);
  if (strncmp(statement, test_lane, sizeof(test_lane) - 1U) == 0)
    return source_assign_once(&make->test_lane,
                              statement + sizeof(test_lane) - 1U, 0, path,
                              line, diagnostic);
  if (strncmp(statement, test_evidence, sizeof(test_evidence) - 1U) == 0)
    return source_assign_once(&make->test_evidence,
                              statement + sizeof(test_evidence) - 1U, 0, path,
                              line, diagnostic);
  if (strncmp(statement, test_target, sizeof(test_target) - 1U) == 0)
    return source_assign_once(&make->test_target,
                              statement + sizeof(test_target) - 1U, 0, path,
                              line, diagnostic);
  if (strncmp(statement, test_machine, sizeof(test_machine) - 1U) == 0)
    return source_assign_once(&make->test_machine,
                              statement + sizeof(test_machine) - 1U, 0, path,
                              line, diagnostic);
  if (strncmp(statement, test_receipt, sizeof(test_receipt) - 1U) == 0)
    return source_assign_once(&make->test_receipt,
                              statement + sizeof(test_receipt) - 1U, 0, path,
                              line, diagnostic);
  if (strncmp(statement, test_sources, sizeof(test_sources) - 1U) == 0)
    return source_parse_list(statement + sizeof(test_sources) - 1U,
                             &make->sources, &make->source_count, 0, 0, path,
                             line, diagnostic);
  if (strncmp(statement, test_timeout, sizeof(test_timeout) - 1U) == 0) {
    char *end = NULL;
    unsigned long parsed;
    if (make->has_timeout) return source_error(
        diagnostic, path, line, "test timeout is declared more than once");
    errno = 0;
    value = statement + sizeof(test_timeout) - 1U;
    parsed = strtoul(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed == 0U ||
        parsed > 120000U)
      return source_error(diagnostic, path, line,
                          "test timeout is outside the bounded range");
    make->test_timeout_ms = (uint32_t)parsed;
    make->has_timeout = 1;
    return CONFIT_OK;
  }
  if (size > sizeof(include_prefix) &&
      strncmp(statement, include_prefix, sizeof(include_prefix) - 1U) == 0 &&
      statement[size - 1U] == '>') {
    const size_t include_size =
        size - (sizeof(include_prefix) - 1U) - 1U;
    if (include_size == 0U || include_size > 95U || make->include_name != NULL)
      return source_error(diagnostic, path, line,
                          "restricted Makefile include is invalid");
    make->include_name = (char *)malloc(include_size + 1U);
    if (make->include_name == NULL) return CONFIT_ERR_INTERNAL;
    memcpy(make->include_name, statement + sizeof(include_prefix) - 1U,
           include_size);
    make->include_name[include_size] = '\0';
    *seen_include = 1;
    return CONFIT_OK;
  }
  return source_error(diagnostic, path, line,
                      "restricted Makefile contains an unknown statement");
}

static ConfitStatus source_parse_makefile(const char *path,
                                          ConfitRestrictedMake *out,
                                          ConfitDiagnostic *diagnostic) {
  char *text = NULL;
  size_t size = 0U;
  size_t offset = 0U;
  size_t line = 1U;
  size_t statement_line = 1U;
  size_t logical_size = 0U;
  char logical[CONFIT_SOURCE_MAX_LINE + 1U];
  int seen_include = 0;
  ConfitStatus status;
  memset(out, 0, sizeof(*out));
  logical[0] = '\0';
  status = confit_host_read_text_file(path, &text, &size, diagnostic);
  if (status != CONFIT_OK) return status;
  if (size == 0U || size > CONFIT_SOURCE_MAX_BYTES ||
      memchr(text, '\0', size) != NULL ||
      !source_utf8((const unsigned char *)text, size)) {
    status = source_error(diagnostic, path, 0U,
                          "restricted Makefile violates the byte bound");
    goto done;
  }
  while (offset < size && status == CONFIT_OK) {
    size_t end = offset;
    size_t length;
    int continuation = 0;
    char saved;
    char *part;
    while (end < size && text[end] != '\n') ++end;
    length = end - offset;
    if (length > 0U && text[offset + length - 1U] == '\r') --length;
    if (length > CONFIT_SOURCE_MAX_LINE) {
      status = source_error(diagnostic, path, line,
                            "restricted Makefile line exceeds the bound");
      break;
    }
    saved = text[offset + length];
    text[offset + length] = '\0';
    part = source_trim(text + offset);
    if (part[0] != '\0' && part[strlen(part) - 1U] == '\\') {
      continuation = 1;
      part[strlen(part) - 1U] = '\0';
      part = source_trim(part);
    } else if (strchr(part, '\\') != NULL) {
      status = source_error(diagnostic, path, line,
                            "restricted Makefile has unsafe continuation");
    }
    if (status == CONFIT_OK && logical_size != 0U &&
        (part[0] == '\0' || part[0] == '#'))
      status = source_error(diagnostic, path, line,
                            "restricted Make continuation is incomplete");
    if (status == CONFIT_OK && part[0] != '\0') {
      const size_t part_size = strlen(part);
      const size_t separator = logical_size != 0U ? 1U : 0U;
      if (logical_size + separator + part_size > CONFIT_SOURCE_MAX_LINE) {
        status = source_error(diagnostic, path, line,
                              "restricted Make statement exceeds the bound");
      } else {
        if (separator != 0U) logical[logical_size++] = ' ';
        memcpy(logical + logical_size, part, part_size + 1U);
        logical_size += part_size;
      }
    }
    text[offset + length] = saved;
    if (status == CONFIT_OK && !continuation) {
      status = source_parse_statement(logical, path, statement_line, out,
                                      &seen_include, diagnostic);
      logical_size = 0U;
      logical[0] = '\0';
      statement_line = line + 1U;
    }
    offset = end < size ? end + 1U : end;
    ++line;
  }
  if (status == CONFIT_OK && (logical_size != 0U || out->api != 3 ||
                              !seen_include || out->include_name == NULL))
    status = source_error(diagnostic, path, line,
                          "restricted Makefile is incomplete");
done:
  confit_host_free(text);
  if (status != CONFIT_OK) restricted_make_clear(out);
  return status;
}

static ConfitStatus source_validate_owned_files(
    const char *project_root, const char *directory, const char *makefile,
    char *const *items, size_t count, ConfitDiagnostic *diagnostic) {
  size_t index;
  for (index = 0U; index < count; ++index) {
    char path[4096];
    char canonical[4096];
    ConfitStatus status = confit_host_path_join(path, sizeof(path), directory,
                                                items[index], diagnostic);
    if (status != CONFIT_OK ||
        confit_host_path_canonicalize(canonical, sizeof(canonical), path,
                                      diagnostic) != CONFIT_OK ||
        strcmp(path, canonical) != 0 || !source_path_within(directory, canonical) ||
        !source_path_within(project_root, canonical) ||
        !confit_host_file_exists(canonical))
      return source_error(diagnostic, makefile, 0U,
                          "declared source is missing, symlinked, or outside its owner");
  }
  return CONFIT_OK;
}

static void nucleus_unit_clear(ConfitNucleusUnit *unit) {
  free(unit->id);
  free(unit->directory);
  free(unit->makefile_path);
  source_list_clear(unit->sources, unit->source_count);
  source_list_clear(unit->uses, unit->use_count);
  source_list_clear(unit->kapi_imports, unit->kapi_import_count);
  source_list_clear(unit->kapi_exports, unit->kapi_export_count);
  source_list_clear(unit->public_headers, unit->public_header_count);
  memset(unit, 0, sizeof(*unit));
}

void confit_nucleus_catalog_clear(ConfitNucleusCatalog *catalog) {
  size_t index;
  if (catalog == NULL) return;
  for (index = 0U; index < catalog->unit_count; ++index)
    nucleus_unit_clear(&catalog->units[index]);
  free(catalog->units);
  free(catalog->project_root);
  memset(catalog, 0, sizeof(*catalog));
}

static ConfitStatus nucleus_append(ConfitNucleusCatalog *catalog,
                                   const char *directory,
                                   const char *makefile,
                                   ConfitRestrictedMake *parsed,
                                   ConfitDiagnostic *diagnostic) {
  ConfitNucleusUnit *grown;
  ConfitNucleusUnit *unit;
  size_t index;
  if (catalog->unit_count >= CONFIT_SOURCE_MAX_UNITS)
    return source_error(diagnostic, makefile, 0U,
                        "nucleus unit count exceeds the supported bound");
  for (index = 0U; index < catalog->unit_count; ++index) {
    if (strcmp(catalog->units[index].id, parsed->unit) == 0)
      return source_error(diagnostic, makefile, 0U,
                          "nucleus unit ID has multiple owners");
  }
  grown = (ConfitNucleusUnit *)realloc(
      catalog->units, (catalog->unit_count + 1U) * sizeof(*grown));
  if (grown == NULL) return CONFIT_ERR_INTERNAL;
  catalog->units = grown;
  unit = &catalog->units[catalog->unit_count];
  memset(unit, 0, sizeof(*unit));
  unit->id = parsed->unit;
  parsed->unit = NULL;
  unit->directory = source_strdup(directory + strlen(catalog->project_root) + 1U);
  unit->makefile_path =
      source_strdup(makefile + strlen(catalog->project_root) + 1U);
  unit->sources = parsed->sources;
  unit->source_count = parsed->source_count;
  parsed->sources = NULL;
  parsed->source_count = 0U;
  unit->uses = parsed->uses;
  unit->use_count = parsed->use_count;
  parsed->uses = NULL;
  parsed->use_count = 0U;
  unit->kapi_imports = parsed->kapi_imports;
  unit->kapi_import_count = parsed->kapi_import_count;
  parsed->kapi_imports = NULL;
  parsed->kapi_import_count = 0U;
  unit->kapi_exports = parsed->kapi_exports;
  unit->kapi_export_count = parsed->kapi_export_count;
  parsed->kapi_exports = NULL;
  parsed->kapi_export_count = 0U;
  unit->public_headers = parsed->public_headers;
  unit->public_header_count = parsed->public_header_count;
  parsed->public_headers = NULL;
  parsed->public_header_count = 0U;
  if (unit->directory == NULL || unit->makefile_path == NULL)
    return CONFIT_ERR_INTERNAL;
  ++catalog->unit_count;
  return CONFIT_OK;
}

static ConfitStatus nucleus_walk(ConfitNucleusCatalog *catalog,
                                 const char *directory, size_t depth,
                                 ConfitDiagnostic *diagnostic) {
  char canonical[4096];
  char manifest[4096];
  char makefile[4096];
  char makefile_canonical[4096];
  ConfitRestrictedMake parsed;
  ConfitStatus status;
  size_t index;
  if (depth > CONFIT_SOURCE_MAX_DEPTH)
    return source_error(diagnostic, directory, 0U,
                        "nucleus hierarchy exceeds the supported depth");
  status = confit_host_path_canonicalize(canonical, sizeof(canonical),
                                         directory, diagnostic);
  if (status != CONFIT_OK || strcmp(canonical, directory) != 0 ||
      !source_path_within(catalog->project_root, canonical))
    return source_error(diagnostic, directory, 0U,
                        "nucleus directory is symlinked or outside the project");
  status = confit_host_path_join(manifest, sizeof(manifest), directory,
                                 "component.toml", diagnostic);
  if (status != CONFIT_OK) return status;
  if (confit_host_file_exists(manifest))
    return source_error(diagnostic, manifest, 0U,
                        "mandatory nucleus directory must not contain component.toml");
  status = confit_host_path_join(makefile, sizeof(makefile), directory,
                                 "Makefile", diagnostic);
  if (status != CONFIT_OK ||
      confit_host_path_canonicalize(makefile_canonical,
                                    sizeof(makefile_canonical), makefile,
                                    diagnostic) != CONFIT_OK ||
      strcmp(makefile, makefile_canonical) != 0)
    return source_error(diagnostic, makefile, 0U,
                        "nucleus Makefile is missing or symlinked");
  status = source_parse_makefile(makefile, &parsed, diagnostic);
  if (status != CONFIT_OK) return status;
  if (strcmp(parsed.include_name, "parus.kernunit.mk") == 0) {
    if (parsed.unit == NULL || parsed.source_count == 0U ||
        parsed.subdir_count != 0U || parsed.test_id != NULL) {
      status = source_error(diagnostic, makefile, 0U,
                            "nucleus leaf has parent or test authority");
    }
    if (status == CONFIT_OK)
      status = source_validate_owned_files(catalog->project_root, directory,
                                           makefile, parsed.sources,
                                           parsed.source_count, diagnostic);
    if (status == CONFIT_OK)
      status = source_validate_owned_files(catalog->project_root, directory,
                                           makefile, parsed.public_headers,
                                           parsed.public_header_count,
                                           diagnostic);
    if (status == CONFIT_OK)
      status = nucleus_append(catalog, directory, makefile, &parsed,
                              diagnostic);
  } else if (strcmp(parsed.include_name, "parus.kernsubdir.mk") == 0) {
    if (parsed.unit != NULL || parsed.source_count != 0U ||
        parsed.subdir_count == 0U || parsed.test_id != NULL) {
      status = source_error(diagnostic, makefile, 0U,
                            "nucleus parent has leaf or test authority");
    }
    for (index = 0U; status == CONFIT_OK && index < parsed.subdir_count;
         ++index) {
      char child[4096];
      if (strchr(parsed.subdirs[index], '/') != NULL ||
          !source_atom(parsed.subdirs[index], 0)) {
        status = source_error(diagnostic, makefile, 0U,
                              "nucleus parent names an unsafe direct child");
      } else {
        status = confit_host_path_join(child, sizeof(child), directory,
                                       parsed.subdirs[index], diagnostic);
      }
      if (status == CONFIT_OK)
        status = nucleus_walk(catalog, child, depth + 1U, diagnostic);
    }
  } else {
    status = source_error(diagnostic, makefile, 0U,
                          "nucleus Makefile uses a non-nucleus public include");
  }
  restricted_make_clear(&parsed);
  return status;
}

static int nucleus_unit_compare(const void *left, const void *right) {
  const ConfitNucleusUnit *a = (const ConfitNucleusUnit *)left;
  const ConfitNucleusUnit *b = (const ConfitNucleusUnit *)right;
  return strcmp(a->id, b->id);
}

static ptrdiff_t nucleus_find(const ConfitNucleusCatalog *catalog,
                              const char *id) {
  size_t index;
  for (index = 0U; index < catalog->unit_count; ++index)
    if (strcmp(catalog->units[index].id, id) == 0) return (ptrdiff_t)index;
  return -1;
}

static ConfitStatus nucleus_visit(const ConfitNucleusCatalog *catalog,
                                  size_t index, unsigned char *state,
                                  size_t depth,
                                  ConfitDiagnostic *diagnostic) {
  size_t edge;
  if (depth > CONFIT_SOURCE_MAX_DEPTH)
    return source_error(diagnostic, catalog->units[index].makefile_path, 0U,
                        "nucleus owner graph exceeds the supported depth");
  if (state[index] == 1U)
    return source_error(diagnostic, catalog->units[index].makefile_path, 0U,
                        "nucleus KERN_USES graph contains a cycle");
  if (state[index] == 2U) return CONFIT_OK;
  state[index] = 1U;
  for (edge = 0U; edge < catalog->units[index].use_count; ++edge) {
    const ptrdiff_t dependency =
        nucleus_find(catalog, catalog->units[index].uses[edge]);
    ConfitStatus status;
    if (dependency < 0)
      return source_error(diagnostic, catalog->units[index].makefile_path, 0U,
                          "nucleus KERN_USES names an undeclared unit");
    if ((size_t)dependency == index)
      return source_error(diagnostic, catalog->units[index].makefile_path, 0U,
                          "nucleus unit cannot depend on itself");
    status = nucleus_visit(catalog, (size_t)dependency, state, depth + 1U,
                           diagnostic);
    if (status != CONFIT_OK) return status;
  }
  state[index] = 2U;
  return CONFIT_OK;
}

static ConfitStatus nucleus_validate(const ConfitNucleusCatalog *catalog,
                                     ConfitDiagnostic *diagnostic) {
  unsigned char *state;
  size_t index;
  size_t other;
  size_t edge_count = 0U;
  state = (unsigned char *)calloc(catalog->unit_count, sizeof(*state));
  if (catalog->unit_count != 0U && state == NULL) return CONFIT_ERR_INTERNAL;
  for (index = 0U; index < catalog->unit_count; ++index) {
    size_t source_index;
    if (catalog->units[index].use_count >
        CONFIT_SOURCE_MAX_EDGES - edge_count) {
      free(state);
      return source_error(diagnostic, catalog->units[index].makefile_path, 0U,
                          "nucleus KERN_USES edge count exceeds the supported bound");
    }
    edge_count += catalog->units[index].use_count;
    ConfitStatus status = nucleus_visit(catalog, index, state, 0U, diagnostic);
    if (status != CONFIT_OK) {
      free(state);
      return status;
    }
    for (source_index = 0U; source_index < catalog->units[index].source_count;
         ++source_index) {
      char owned[2048];
      const int written = snprintf(owned, sizeof(owned), "%s/%s",
                                   catalog->units[index].directory,
                                   catalog->units[index].sources[source_index]);
      if (written <= 0 || (size_t)written >= sizeof(owned)) {
        free(state);
        return CONFIT_ERR_INTERNAL;
      }
      for (other = index + 1U; other < catalog->unit_count; ++other) {
        size_t candidate;
        for (candidate = 0U;
             candidate < catalog->units[other].source_count; ++candidate) {
          char other_owned[2048];
          const int other_written = snprintf(
              other_owned, sizeof(other_owned), "%s/%s",
              catalog->units[other].directory,
              catalog->units[other].sources[candidate]);
          if (other_written <= 0 ||
              (size_t)other_written >= sizeof(other_owned)) {
            free(state);
            return CONFIT_ERR_INTERNAL;
          }
          if (strcmp(owned, other_owned) == 0) {
            free(state);
            return source_error(
                diagnostic, catalog->units[index].makefile_path, 0U,
                "mandatory source has multiple KERN_UNIT owners");
          }
        }
      }
    }
  }
  free(state);
  return CONFIT_OK;
}

ConfitStatus confit_nucleus_catalog_load(const ConfitV2Project *project,
                                         ConfitNucleusCatalog *out_catalog,
                                         ConfitDiagnostic *diagnostic) {
  size_t index;
  ConfitStatus status = CONFIT_OK;
  if (project == NULL || project->project_root == NULL || out_catalog == NULL)
    return CONFIT_ERR_INVALID_ARGUMENT;
  memset(out_catalog, 0, sizeof(*out_catalog));
  if (project->nucleus_roots.count == 0U) return CONFIT_OK;
  if (project->nucleus_roots.count > CONFIT_SOURCE_MAX_ITEMS)
    return source_error(diagnostic, project->span.path, 0U,
                        "project must declare bounded nucleus_roots");
  out_catalog->project_root = source_strdup(project->project_root);
  if (out_catalog->project_root == NULL) return CONFIT_ERR_INTERNAL;
  for (index = 0U; status == CONFIT_OK &&
                   index < project->nucleus_roots.count;
       ++index) {
    char root[4096];
    if (!source_relative(project->nucleus_roots.items[index], 1)) {
      status = source_error(diagnostic, project->span.path, 0U,
                            "project nucleus root is unsafe");
    } else {
      status = confit_host_path_join(root, sizeof(root), project->project_root,
                                     project->nucleus_roots.items[index],
                                     diagnostic);
    }
    if (status == CONFIT_OK) status = nucleus_walk(out_catalog, root, 0U, diagnostic);
  }
  if (status == CONFIT_OK && out_catalog->unit_count == 0U)
    status = source_error(diagnostic, project->span.path, 0U,
                          "nucleus graph contains no leaf units");
  if (status == CONFIT_OK) {
    qsort(out_catalog->units, out_catalog->unit_count,
          sizeof(*out_catalog->units), nucleus_unit_compare);
    status = nucleus_validate(out_catalog, diagnostic);
  }
  if (status != CONFIT_OK) confit_nucleus_catalog_clear(out_catalog);
  return status;
}

static int source_list_has(char *const *items, size_t count,
                           const char *value) {
  size_t index;
  for (index = 0U; index < count; ++index)
    if (strcmp(items[index], value) == 0) return 1;
  return 0;
}

static size_t static_kapi_provider_count(
    const ConfitComponentClosure *components,
    const ConfitNucleusCatalog *nucleus, const char *kapi) {
  size_t count = 0U;
  size_t index;
  for (index = 0U; components != NULL && index < components->component_count;
       ++index) {
    const ConfitComponent *component = components->ordered[index];
    if (source_list_has(component->kapi_provides,
                        component->kapi_provide_count, kapi))
      ++count;
  }
  for (index = 0U; nucleus != NULL && index < nucleus->unit_count; ++index)
    if (source_list_has(nucleus->units[index].kapi_exports,
                        nucleus->units[index].kapi_export_count, kapi))
      ++count;
  return count;
}

ConfitStatus confit_static_kapi_validate(
    const ConfitComponentClosure *components,
    const ConfitNucleusCatalog *nucleus,
    ConfitDiagnostic *diagnostic) {
  size_t index;
  if (components == NULL || nucleus == NULL)
    return CONFIT_ERR_INVALID_ARGUMENT;
  for (index = 0U; index < components->component_count; ++index) {
    const ConfitComponent *component = components->ordered[index];
    size_t item;
    for (item = 0U; item < component->kapi_provide_count; ++item) {
      if (static_kapi_provider_count(components, nucleus,
                                     component->kapi_provides[item]) != 1U)
        return source_error(diagnostic, component->manifest_path,
                            component->kapi_provide_spans[item].line,
                            "static KAPI has multiple selected providers");
    }
    for (item = 0U; item < component->kapi_requirement_count; ++item) {
      if (static_kapi_provider_count(components, nucleus,
                                     component->kapi_requires[item]) != 1U)
        return source_error(diagnostic, component->manifest_path,
                            component->kapi_requirement_spans[item].line,
                            "selected KAPI import has no exact provider");
    }
  }
  for (index = 0U; index < nucleus->unit_count; ++index) {
    const ConfitNucleusUnit *unit = &nucleus->units[index];
    size_t item;
    for (item = 0U; item < unit->kapi_export_count; ++item) {
      if (static_kapi_provider_count(components, nucleus,
                                     unit->kapi_exports[item]) != 1U)
        return source_error(diagnostic, unit->makefile_path, 0U,
                            "nucleus KAPI has multiple selected providers");
    }
    for (item = 0U; item < unit->kapi_import_count; ++item) {
      if (static_kapi_provider_count(components, nucleus,
                                     unit->kapi_imports[item]) != 1U)
        return source_error(diagnostic, unit->makefile_path, 0U,
                            "nucleus KAPI import has no exact provider");
    }
  }
  return CONFIT_OK;
}

static void test_unit_clear(ConfitTestUnit *test) {
  free(test->id);
  free(test->owner);
  free(test->lane);
  free(test->evidence_class);
  free(test->target);
  free(test->machine_profile);
  free(test->receipt_profile);
  free(test->directory);
  free(test->makefile_path);
  source_list_clear(test->sources, test->source_count);
  memset(test, 0, sizeof(*test));
}

void confit_test_catalog_clear(ConfitTestCatalog *catalog) {
  size_t index;
  if (catalog == NULL) return;
  for (index = 0U; index < catalog->test_count; ++index)
    test_unit_clear(&catalog->tests[index]);
  free(catalog->tests);
  free(catalog->project_root);
  memset(catalog, 0, sizeof(*catalog));
}

static int test_compare(const void *left, const void *right) {
  const ConfitTestUnit *a = (const ConfitTestUnit *)left;
  const ConfitTestUnit *b = (const ConfitTestUnit *)right;
  return strcmp(a->id, b->id);
}

static int test_lane_valid(const char *lane, const char *evidence) {
  return (strcmp(lane, "unit") == 0 && strcmp(evidence, "host-unit") == 0) ||
         (strcmp(lane, "security") == 0 &&
          strcmp(evidence, "host-security") == 0) ||
         (strcmp(lane, "selftest") == 0 &&
          strcmp(evidence, "booted-selftest") == 0) ||
         (strcmp(lane, "qemu") == 0 &&
          (strcmp(evidence, "qemu-runtime") == 0 ||
           strcmp(evidence, "qemu-smoke") == 0)) ||
         (strcmp(lane, "package") == 0 && strcmp(evidence, "package") == 0);
}

static ConfitStatus test_append(ConfitTestCatalog *catalog,
                                const char *directory, const char *makefile,
                                ConfitRestrictedMake *parsed,
                                ConfitDiagnostic *diagnostic) {
  ConfitTestUnit *grown;
  ConfitTestUnit *test;
  size_t index;
  if (catalog->test_count >= CONFIT_SOURCE_MAX_TESTS)
    return source_error(diagnostic, makefile, 0U,
                        "test count exceeds the supported bound");
  if (parsed->test_id == NULL || parsed->test_owner == NULL ||
      parsed->test_lane == NULL || parsed->test_evidence == NULL ||
      !parsed->has_timeout || parsed->unit != NULL ||
      parsed->subdir_count != 0U || parsed->use_count != 0U ||
      parsed->kapi_export_count != 0U || parsed->public_header_count != 0U ||
      !test_lane_valid(parsed->test_lane, parsed->test_evidence))
    return source_error(diagnostic, makefile, 0U,
                        "test Makefile has incomplete or mixed authority");
  if ((strcmp(parsed->test_lane, "unit") == 0 ||
       strcmp(parsed->test_lane, "security") == 0) &&
      parsed->source_count == 0U)
    return source_error(diagnostic, makefile, 0U,
                        "host test must declare at least one source");
  for (index = 0U; index < catalog->test_count; ++index)
    if (strcmp(catalog->tests[index].id, parsed->test_id) == 0)
      return source_error(diagnostic, makefile, 0U,
                          "test ID has multiple local owners");
  grown = (ConfitTestUnit *)realloc(
      catalog->tests, (catalog->test_count + 1U) * sizeof(*grown));
  if (grown == NULL) return CONFIT_ERR_INTERNAL;
  catalog->tests = grown;
  test = &catalog->tests[catalog->test_count];
  memset(test, 0, sizeof(*test));
#define TEST_MOVE(member, parsed_member) \
  test->member = parsed->parsed_member;   \
  parsed->parsed_member = NULL
  TEST_MOVE(id, test_id);
  TEST_MOVE(owner, test_owner);
  TEST_MOVE(lane, test_lane);
  TEST_MOVE(evidence_class, test_evidence);
  TEST_MOVE(target, test_target);
  TEST_MOVE(machine_profile, test_machine);
  TEST_MOVE(receipt_profile, test_receipt);
#undef TEST_MOVE
  test->timeout_ms = parsed->test_timeout_ms;
  test->directory = source_strdup(directory + strlen(catalog->project_root) + 1U);
  test->makefile_path =
      source_strdup(makefile + strlen(catalog->project_root) + 1U);
  test->sources = parsed->sources;
  test->source_count = parsed->source_count;
  parsed->sources = NULL;
  parsed->source_count = 0U;
  if (test->target == NULL) test->target = source_strdup("none");
  if (test->machine_profile == NULL)
    test->machine_profile = source_strdup("none");
  if (test->receipt_profile == NULL)
    test->receipt_profile = source_strdup("none");
  if (test->directory == NULL || test->makefile_path == NULL ||
      test->target == NULL || test->machine_profile == NULL ||
      test->receipt_profile == NULL)
    return CONFIT_ERR_INTERNAL;
  if ((strcmp(test->lane, "selftest") == 0 || strcmp(test->lane, "qemu") == 0 ||
       strcmp(test->lane, "package") == 0) && strcmp(test->target, "none") == 0)
    return source_error(diagnostic, makefile, 0U,
                        "runtime/package test omits its target");
  if ((strcmp(test->lane, "selftest") == 0 || strcmp(test->lane, "qemu") == 0) &&
      (strcmp(test->machine_profile, "none") == 0 ||
       strcmp(test->receipt_profile, "none") == 0))
    return source_error(diagnostic, makefile, 0U,
                        "QEMU test omits machine or receipt profile");
  ++catalog->test_count;
  return CONFIT_OK;
}

ConfitStatus confit_test_catalog_load(const ConfitV2Project *project,
                                      ConfitTestCatalog *out_catalog,
                                      ConfitDiagnostic *diagnostic) {
  size_t root_index;
  ConfitStatus status = CONFIT_OK;
  if (project == NULL || project->project_root == NULL || out_catalog == NULL)
    return CONFIT_ERR_INVALID_ARGUMENT;
  memset(out_catalog, 0, sizeof(*out_catalog));
  if (project->test_roots.count == 0U) return CONFIT_OK;
  if (project->test_roots.count > CONFIT_SOURCE_MAX_ITEMS)
    return source_error(diagnostic, project->span.path, 0U,
                        "project must declare bounded test_roots");
  out_catalog->project_root = source_strdup(project->project_root);
  if (out_catalog->project_root == NULL) return CONFIT_ERR_INTERNAL;
  for (root_index = 0U; status == CONFIT_OK &&
                        root_index < project->test_roots.count;
       ++root_index) {
    char root[4096];
    char **paths = NULL;
    size_t path_count = 0U;
    size_t path_index;
    if (!source_relative(project->test_roots.items[root_index], 1)) {
      status = source_error(diagnostic, project->span.path, 0U,
                            "project test root is unsafe");
      break;
    }
    status = confit_host_path_join(root, sizeof(root), project->project_root,
                                   project->test_roots.items[root_index],
                                   diagnostic);
    if (status == CONFIT_OK)
      status = confit_host_list_named_files_recursive(
          root, "Makefile", CONFIT_SOURCE_MAX_DEPTH, 4096U,
          CONFIT_SOURCE_MAX_BYTES, &paths, &path_count, diagnostic);
    for (path_index = 0U; status == CONFIT_OK && path_index < path_count;
         ++path_index) {
      char *text = NULL;
      size_t size = 0U;
      ConfitRestrictedMake parsed;
      const char *separator;
      char directory[4096];
      status = confit_host_read_text_file(paths[path_index], &text, &size,
                                          diagnostic);
      if (status != CONFIT_OK) break;
      if (strstr(text, ".include <parus.test.mk>") == NULL) {
        confit_host_free(text);
        continue;
      }
      confit_host_free(text);
      separator = strrchr(paths[path_index], '/');
      if (separator == NULL ||
          (size_t)(separator - paths[path_index]) >= sizeof(directory)) {
        status = CONFIT_ERR_INTERNAL;
        break;
      }
      memcpy(directory, paths[path_index],
             (size_t)(separator - paths[path_index]));
      directory[separator - paths[path_index]] = '\0';
      status = source_parse_makefile(paths[path_index], &parsed, diagnostic);
      if (status == CONFIT_OK &&
          strcmp(parsed.include_name, "parus.test.mk") != 0)
        status = source_error(diagnostic, paths[path_index], 0U,
                              "test Makefile uses a non-test public include");
      if (status == CONFIT_OK)
        status = source_validate_owned_files(
            project->project_root, directory, paths[path_index], parsed.sources,
            parsed.source_count, diagnostic);
      if (status == CONFIT_OK)
        status = test_append(out_catalog, directory, paths[path_index], &parsed,
                             diagnostic);
      restricted_make_clear(&parsed);
    }
    confit_host_string_list_free(paths, path_count);
  }
  if (status == CONFIT_OK && out_catalog->test_count == 0U)
    status = source_error(diagnostic, project->span.path, 0U,
                          "test graph contains no local tests");
  if (status == CONFIT_OK)
    qsort(out_catalog->tests, out_catalog->test_count,
          sizeof(*out_catalog->tests), test_compare);
  if (status != CONFIT_OK) confit_test_catalog_clear(out_catalog);
  return status;
}

ConfitStatus confit_test_catalog_validate_owners(
    const ConfitTestCatalog *tests, const ConfitNucleusCatalog *nucleus,
    const ConfitComponentCatalog *components, ConfitDiagnostic *diagnostic) {
  size_t test_index;
  if (tests == NULL || nucleus == NULL || components == NULL)
    return CONFIT_ERR_INVALID_ARGUMENT;
  for (test_index = 0U; test_index < tests->test_count; ++test_index) {
    const ConfitTestUnit *test = &tests->tests[test_index];
    size_t owner_index;
    int found = 0;
    for (owner_index = 0U; owner_index < nucleus->unit_count; ++owner_index) {
      if (strcmp(test->owner, nucleus->units[owner_index].id) == 0) {
        found = 1;
        break;
      }
    }
    if (!found && confit_component_catalog_find(components, test->owner) != NULL)
      found = 1;
    if (!found && strcmp(test->target, "none") != 0) {
      char target_owner[256];
      const int written = snprintf(target_owner, sizeof(target_owner),
                                   "target.%s", test->target);
      if (written > 0 && (size_t)written < sizeof(target_owner) &&
          strcmp(test->owner, target_owner) == 0)
        found = 1;
    }
    if (!found)
      return source_error(diagnostic, test->makefile_path, 0U,
                          "test owner is not an exact nucleus, selectable component, or target identity");
  }
  return CONFIT_OK;
}
