#include "confit/compat_v2.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/expression_v2.h"
#include "confit/parser_v2.h"

typedef struct ConfitV2CompatSpan {
  char *path;
  size_t line;
  size_t column;
} ConfitV2CompatSpan;

typedef struct ConfitV2CompatExpressionSource {
  char *text;
  ConfitV2CompatSpan span;
} ConfitV2CompatExpressionSource;

typedef struct ConfitV2CompatAlias {
  char *name;
  char *declared_path;
  ConfitV2CompatSpan span;
} ConfitV2CompatAlias;

typedef struct ConfitV2CompatConstraint {
  char *id;
  ConfitV2CompatExpressionSource when;
  ConfitV2CompatExpressionSource condition;
  ConfitV2CompatAction action;
  char *message;
  ConfitV2CompatSpan span;
} ConfitV2CompatConstraint;

struct ConfitV2CompatSuite {
  char *name;
  char *source_path;
  uint64_t source_hash;
  ConfitV2CompatAlias *aliases;
  size_t alias_count;
  ConfitV2CompatConstraint *constraints;
  size_t constraint_count;
};

typedef struct ConfitV2CompatReportProject {
  char *alias;
  char *name;
  char *profile;
  char *target;
  uint64_t source_hash;
  uint64_t input_hash;
  uint64_t snapshot_hash;
} ConfitV2CompatReportProject;

typedef struct ConfitV2CompatTrace {
  char *alias;
  char *option_id;
  ConfitV2OptionType type;
  int effective_is_set;
  ConfitV2EffectiveValueOrigin effective_origin;
  ConfitV2Value effective_value;
  char *source_path;
  size_t source_line;
  size_t source_column;
} ConfitV2CompatTrace;

typedef struct ConfitV2CompatReportResult {
  char *id;
  char *when_text;
  char *condition_text;
  char *message;
  ConfitV2CompatAction action;
  ConfitV2CompatResultState state;
  ConfitV2CompatSpan span;
  ConfitV2CompatTrace *traces;
  size_t trace_count;
} ConfitV2CompatReportResult;

struct ConfitV2CompatReport {
  char *suite_name;
  char *source_path;
  uint64_t source_hash;
  ConfitV2CompatReportProject *projects;
  size_t project_count;
  ConfitV2CompatReportResult *results;
  size_t result_count;
  size_t pass_count;
  size_t fail_count;
  size_t not_applicable_count;
};

typedef struct ConfitV2CompatBinding {
  char *internal_id;
  const char *alias;
  const ConfitV2SnapshotOption *option;
  ConfitV2ExpressionBinding binding;
} ConfitV2CompatBinding;

typedef struct ConfitV2CompatBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitV2CompatBuilder;

static const char kInvalidArgument[] = "invalid schema v2 compatibility argument";
static const char kAllocationFailed[] =
    "failed to allocate schema v2 compatibility data";
static const char kInvalidSource[] = "invalid schema v2 compatibility source";
static const char kUnknownSourceKey[] = "unknown schema v2 compatibility field";
static const char kInvalidSchemaVersion[] =
    "schema v2 compatibility source requires schema_version = 2";
static const char kInvalidAction[] =
    "compatibility constraint needs exactly one require or forbid expression";
static const char kInvalidAlias[] = "invalid schema v2 compatibility alias";
static const char kDuplicateId[] = "duplicate schema v2 compatibility constraint id";
static const char kMixedSchema[] =
    "v1 and v2 snapshots cannot be mixed in compatibility checking";
static const char kArtifactAbiMismatch[] =
    "schema v2 compatibility artifact ABI mismatch";
static const char kSnapshotIdentityMismatch[] =
    "schema v2 compatibility snapshot identity mismatch";
static const char kAliasMismatch[] =
    "compatibility project aliases do not match the compatibility source";
static const char kBooleanRequired[] =
    "compatibility when and action expressions must evaluate to bool";

static char *confit_v2_compat_strdup(const char *text) {
  size_t size;
  char *copy;

  if (text == 0) {
    return 0;
  }
  size = strlen(text);
  copy = (char *)malloc(size + 1U);
  if (copy != 0) {
    memcpy(copy, text, size + 1U);
  }
  return copy;
}

static char *confit_v2_compat_strndup(const char *text, size_t size) {
  char *copy;

  if (text == 0 || size == SIZE_MAX) {
    return 0;
  }
  copy = (char *)malloc(size + 1U);
  if (copy != 0) {
    if (size > 0U) {
      memcpy(copy, text, size);
    }
    copy[size] = '\0';
  }
  return copy;
}

static uint64_t confit_v2_compat_hash_text(const char *text) {
  uint64_t hash = UINT64_C(1469598103934665603);
  size_t index;

  for (index = 0U; text != 0 && text[index] != '\0'; ++index) {
    hash ^= (unsigned char)text[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static void confit_v2_compat_span_clear(ConfitV2CompatSpan *span) {
  if (span == 0) {
    return;
  }
  free(span->path);
  memset(span, 0, sizeof(*span));
}

static ConfitStatus confit_v2_compat_span_from_toml(
    ConfitV2CompatSpan *out, const ConfitV2TomlValue *value) {
  const char *path;

  if (out == 0 || value == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(out, 0, sizeof(*out));
  path = confit_v2_toml_value_source(value);
  out->path = confit_v2_compat_strdup(path != 0 ? path : "");
  if (out->path == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  out->line = confit_v2_toml_value_line(value);
  out->column = confit_v2_toml_value_column(value);
  return CONFIT_OK;
}

static void confit_v2_compat_expression_source_clear(
    ConfitV2CompatExpressionSource *source) {
  if (source == 0) {
    return;
  }
  free(source->text);
  confit_v2_compat_span_clear(&source->span);
  memset(source, 0, sizeof(*source));
}

static void confit_v2_compat_value_clear(ConfitV2Value *value) {
  size_t index;

  if (value == 0) {
    return;
  }
  if (value->kind == CONFIT_V2_VALUE_STRING) {
    free(value->as.string_value);
  } else if (value->kind == CONFIT_V2_VALUE_STRING_LIST) {
    for (index = 0U; index < value->as.string_list.count; ++index) {
      free(value->as.string_list.items[index]);
    }
    free(value->as.string_list.items);
  }
  memset(value, 0, sizeof(*value));
}

static ConfitStatus confit_v2_compat_value_copy(ConfitV2Value *out,
                                                  const ConfitV2Value *value) {
  size_t index;

  if (out == 0 || value == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(out, 0, sizeof(*out));
  out->kind = value->kind;
  if (value->kind == CONFIT_V2_VALUE_STRING) {
    out->as.string_value = confit_v2_compat_strdup(value->as.string_value);
    return out->as.string_value != 0 ? CONFIT_OK : CONFIT_ERR_INTERNAL;
  }
  if (value->kind == CONFIT_V2_VALUE_STRING_LIST) {
    out->as.string_list.count = value->as.string_list.count;
    if (value->as.string_list.count == 0U) {
      return CONFIT_OK;
    }
    out->as.string_list.items =
        (char **)calloc(value->as.string_list.count, sizeof(*out->as.string_list.items));
    if (out->as.string_list.items == 0) {
      return CONFIT_ERR_INTERNAL;
    }
    for (index = 0U; index < value->as.string_list.count; ++index) {
      out->as.string_list.items[index] =
          confit_v2_compat_strdup(value->as.string_list.items[index]);
      if (out->as.string_list.items[index] == 0) {
        confit_v2_compat_value_clear(out);
        return CONFIT_ERR_INTERNAL;
      }
    }
    return CONFIT_OK;
  }
  out->as = value->as;
  return CONFIT_OK;
}

static void confit_v2_compat_alias_clear(ConfitV2CompatAlias *alias) {
  if (alias == 0) {
    return;
  }
  free(alias->name);
  free(alias->declared_path);
  confit_v2_compat_span_clear(&alias->span);
  memset(alias, 0, sizeof(*alias));
}

static void confit_v2_compat_constraint_clear(ConfitV2CompatConstraint *constraint) {
  if (constraint == 0) {
    return;
  }
  free(constraint->id);
  confit_v2_compat_expression_source_clear(&constraint->when);
  confit_v2_compat_expression_source_clear(&constraint->condition);
  free(constraint->message);
  confit_v2_compat_span_clear(&constraint->span);
  memset(constraint, 0, sizeof(*constraint));
}

void confit_v2_compat_suite_free(ConfitV2CompatSuite *suite) {
  size_t index;

  if (suite == 0) {
    return;
  }
  for (index = 0U; index < suite->alias_count; ++index) {
    confit_v2_compat_alias_clear(&suite->aliases[index]);
  }
  for (index = 0U; index < suite->constraint_count; ++index) {
    confit_v2_compat_constraint_clear(&suite->constraints[index]);
  }
  free(suite->aliases);
  free(suite->constraints);
  free(suite->name);
  free(suite->source_path);
  free(suite);
}

static void confit_v2_compat_diagnostic(ConfitDiagnostic *diagnostic,
                                         ConfitStatus status,
                                         const ConfitV2TomlValue *value,
                                         const char *message) {
  confit_diagnostic_set(diagnostic, status,
                        value != 0 ? confit_v2_toml_value_source(value) : 0,
                        value != 0 ? confit_v2_toml_value_line(value) : 0U,
                        value != 0 ? confit_v2_toml_value_column(value) : 0U,
                        message);
}

static void confit_v2_compat_span_diagnostic(ConfitDiagnostic *diagnostic,
                                              ConfitStatus status,
                                              const ConfitV2CompatSpan *span,
                                              const char *message) {
  confit_diagnostic_set(diagnostic, status, span != 0 ? span->path : 0,
                        span != 0 ? span->line : 0U,
                        span != 0 ? span->column : 0U, message);
}

static int confit_v2_compat_alias_is_valid(const char *text) {
  size_t index;

  if (text == 0 || text[0] < 'a' || text[0] > 'z') {
    return 0;
  }
  for (index = 1U; text[index] != '\0'; ++index) {
    const char value = text[index];
    if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
          value == '_')) {
      return 0;
    }
  }
  return 1;
}

static int confit_v2_compat_key_allowed(const char *key,
                                         const char *const *allowed,
                                         size_t allowed_count) {
  size_t index;

  for (index = 0U; index < allowed_count; ++index) {
    if (strcmp(key, allowed[index]) == 0) {
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_v2_compat_read_string(const ConfitV2TomlValue *value,
                                                  char **out,
                                                  ConfitDiagnostic *diagnostic) {
  const char *text;
  size_t size;

  *out = 0;
  if (value == 0 || !confit_v2_toml_value_string(value, &text, &size)) {
    confit_v2_compat_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, value,
                                kInvalidSource);
    return CONFIT_ERR_SCHEMA;
  }
  *out = confit_v2_compat_strndup(text, size);
  if (*out == 0) {
    confit_v2_compat_diagnostic(diagnostic, CONFIT_ERR_INTERNAL, value,
                                kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_compat_read_expression(
    const ConfitV2TomlValue *value, ConfitV2CompatExpressionSource *out,
    ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  memset(out, 0, sizeof(*out));
  status = confit_v2_compat_read_string(value, &out->text, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_compat_span_from_toml(&out->span, value);
  }
  if (status != CONFIT_OK) {
    confit_v2_compat_expression_source_clear(out);
  }
  return status;
}

static int confit_v2_compat_alias_compare(const void *left, const void *right) {
  const ConfitV2CompatAlias *left_alias = (const ConfitV2CompatAlias *)left;
  const ConfitV2CompatAlias *right_alias = (const ConfitV2CompatAlias *)right;
  return strcmp(left_alias->name, right_alias->name);
}

static int confit_v2_compat_constraint_compare(const void *left,
                                                const void *right) {
  const ConfitV2CompatConstraint *left_constraint =
      (const ConfitV2CompatConstraint *)left;
  const ConfitV2CompatConstraint *right_constraint =
      (const ConfitV2CompatConstraint *)right;
  return strcmp(left_constraint->id, right_constraint->id);
}

static ConfitStatus confit_v2_compat_append_alias(
    ConfitV2CompatSuite *suite, const char *name, const ConfitV2TomlValue *value,
    ConfitDiagnostic *diagnostic) {
  ConfitV2CompatAlias *grown;
  ConfitV2CompatAlias *alias;
  ConfitStatus status;

  if (!confit_v2_compat_alias_is_valid(name)) {
    confit_v2_compat_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, value, kInvalidAlias);
    return CONFIT_ERR_SCHEMA;
  }
  grown = (ConfitV2CompatAlias *)realloc(
      suite->aliases, (suite->alias_count + 1U) * sizeof(*suite->aliases));
  if (grown == 0) {
    confit_v2_compat_diagnostic(diagnostic, CONFIT_ERR_INTERNAL, value,
                                kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  suite->aliases = grown;
  alias = &suite->aliases[suite->alias_count];
  memset(alias, 0, sizeof(*alias));
  alias->name = confit_v2_compat_strdup(name);
  status = alias->name != 0
               ? confit_v2_compat_read_string(value, &alias->declared_path,
                                               diagnostic)
               : CONFIT_ERR_INTERNAL;
  if (status == CONFIT_OK) {
    status = confit_v2_compat_span_from_toml(&alias->span, value);
  }
  if (status != CONFIT_OK) {
    confit_v2_compat_alias_clear(alias);
    if (status == CONFIT_ERR_INTERNAL) {
      confit_v2_compat_diagnostic(diagnostic, status, value, kAllocationFailed);
    }
    return status;
  }
  suite->alias_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_compat_parse_constraint(
    ConfitV2CompatSuite *suite, const ConfitV2TomlValue *table,
    ConfitDiagnostic *diagnostic) {
  static const char *const kKeys[] = {"id", "when", "require", "forbid",
                                        "message"};
  ConfitV2CompatConstraint *grown;
  ConfitV2CompatConstraint *constraint;
  const ConfitV2TomlValue *id;
  const ConfitV2TomlValue *when;
  const ConfitV2TomlValue *require;
  const ConfitV2TomlValue *forbid;
  const ConfitV2TomlValue *message;
  size_t index;
  ConfitStatus status;

  if (confit_v2_toml_value_type(table) != CONFIT_V2_TOML_VALUE_TABLE) {
    confit_v2_compat_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, table,
                                kInvalidSource);
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < confit_v2_toml_table_size(table); ++index) {
    if (!confit_v2_compat_key_allowed(confit_v2_toml_table_key_at(table, index),
                                       kKeys, sizeof(kKeys) / sizeof(kKeys[0]))) {
      confit_v2_compat_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA,
          confit_v2_toml_table_value_at(table, index), kUnknownSourceKey);
      return CONFIT_ERR_SCHEMA;
    }
  }
  id = confit_v2_toml_table_find(table, "id");
  when = confit_v2_toml_table_find(table, "when");
  require = confit_v2_toml_table_find(table, "require");
  forbid = confit_v2_toml_table_find(table, "forbid");
  message = confit_v2_toml_table_find(table, "message");
  if (id == 0 || message == 0 || (require == 0 && forbid == 0) ||
      (require != 0 && forbid != 0)) {
    confit_v2_compat_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, table,
                                require != 0 || forbid != 0 ? kInvalidSource
                                                             : kInvalidAction);
    return CONFIT_ERR_SCHEMA;
  }
  grown = (ConfitV2CompatConstraint *)realloc(
      suite->constraints,
      (suite->constraint_count + 1U) * sizeof(*suite->constraints));
  if (grown == 0) {
    confit_v2_compat_diagnostic(diagnostic, CONFIT_ERR_INTERNAL, table,
                                kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  suite->constraints = grown;
  constraint = &suite->constraints[suite->constraint_count];
  memset(constraint, 0, sizeof(*constraint));
  status = confit_v2_compat_read_string(id, &constraint->id, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_compat_read_string(message, &constraint->message,
                                           diagnostic);
  }
  if (status == CONFIT_OK && when != 0) {
    status = confit_v2_compat_read_expression(when, &constraint->when,
                                               diagnostic);
  }
  if (status == CONFIT_OK && when == 0) {
    constraint->when.text = confit_v2_compat_strdup("true");
    if (constraint->when.text == 0) {
      status = CONFIT_ERR_INTERNAL;
    } else {
      status = confit_v2_compat_span_from_toml(&constraint->when.span,
                                                require != 0 ? require : forbid);
    }
  }
  if (status == CONFIT_OK) {
    constraint->action = require != 0 ? CONFIT_V2_COMPAT_ACTION_REQUIRE
                                      : CONFIT_V2_COMPAT_ACTION_FORBID;
    status = confit_v2_compat_read_expression(require != 0 ? require : forbid,
                                               &constraint->condition,
                                               diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_compat_span_from_toml(&constraint->span, id);
  }
  if (status != CONFIT_OK || constraint->id[0] == '\0') {
    if (status == CONFIT_OK) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_compat_diagnostic(diagnostic, status, id, kInvalidSource);
    } else if (status == CONFIT_ERR_INTERNAL) {
      confit_v2_compat_diagnostic(diagnostic, status, table, kAllocationFailed);
    }
    confit_v2_compat_constraint_clear(constraint);
    return status;
  }
  suite->constraint_count += 1U;
  return CONFIT_OK;
}

ConfitStatus confit_v2_compat_load_file(const char *path,
                                        ConfitV2CompatSuite **out_suite,
                                        ConfitDiagnostic *diagnostic) {
  static const char *const kTopKeys[] = {"compat", "projects", "constraint"};
  static const char *const kCompatKeys[] = {"name", "schema_version"};
  ConfitV2TomlDocument *document = 0;
  const ConfitV2TomlValue *root;
  const ConfitV2TomlValue *compat;
  const ConfitV2TomlValue *projects;
  const ConfitV2TomlValue *constraints;
  const ConfitV2TomlValue *name;
  const ConfitV2TomlValue *schema_version;
  ConfitV2CompatSuite *suite = 0;
  int64_t version;
  size_t index;
  ConfitStatus status;

  if (out_suite == 0 || path == 0 || path[0] == '\0') {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, path, 0U, 0U,
                          kInvalidArgument);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_suite = 0;
  status = confit_v2_toml_parse_file(path, &document, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  root = confit_v2_toml_document_root(document);
  compat = confit_v2_toml_table_find(root, "compat");
  projects = confit_v2_toml_table_find(root, "projects");
  constraints = confit_v2_toml_table_find(root, "constraint");
  if (confit_v2_toml_value_type(root) != CONFIT_V2_TOML_VALUE_TABLE || compat == 0 ||
      projects == 0 || constraints == 0) {
    confit_v2_compat_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, root,
                                kInvalidSource);
    status = CONFIT_ERR_SCHEMA;
    goto done;
  }
  for (index = 0U; index < confit_v2_toml_table_size(root); ++index) {
    if (!confit_v2_compat_key_allowed(confit_v2_toml_table_key_at(root, index),
                                       kTopKeys, sizeof(kTopKeys) / sizeof(kTopKeys[0]))) {
      confit_v2_compat_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA,
          confit_v2_toml_table_value_at(root, index), kUnknownSourceKey);
      status = CONFIT_ERR_SCHEMA;
      goto done;
    }
  }
  if (confit_v2_toml_value_type(compat) != CONFIT_V2_TOML_VALUE_TABLE ||
      confit_v2_toml_value_type(projects) != CONFIT_V2_TOML_VALUE_TABLE ||
      confit_v2_toml_value_type(constraints) != CONFIT_V2_TOML_VALUE_ARRAY) {
    confit_v2_compat_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, compat,
                                kInvalidSource);
    status = CONFIT_ERR_SCHEMA;
    goto done;
  }
  for (index = 0U; index < confit_v2_toml_table_size(compat); ++index) {
    if (!confit_v2_compat_key_allowed(confit_v2_toml_table_key_at(compat, index),
                                       kCompatKeys,
                                       sizeof(kCompatKeys) / sizeof(kCompatKeys[0]))) {
      confit_v2_compat_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA,
          confit_v2_toml_table_value_at(compat, index), kUnknownSourceKey);
      status = CONFIT_ERR_SCHEMA;
      goto done;
    }
  }
  name = confit_v2_toml_table_find(compat, "name");
  schema_version = confit_v2_toml_table_find(compat, "schema_version");
  if (name == 0 || schema_version == 0 ||
      !confit_v2_toml_value_int64(schema_version, &version) ||
      version != (int64_t)CONFIT_V2_COMPAT_SCHEMA_VERSION) {
    confit_v2_compat_diagnostic(diagnostic, CONFIT_ERR_SCHEMA,
                                schema_version != 0 ? schema_version : compat,
                                kInvalidSchemaVersion);
    status = CONFIT_ERR_SCHEMA;
    goto done;
  }
  suite = (ConfitV2CompatSuite *)calloc(1U, sizeof(*suite));
  if (suite == 0) {
    confit_v2_compat_diagnostic(diagnostic, CONFIT_ERR_INTERNAL, compat,
                                kAllocationFailed);
    status = CONFIT_ERR_INTERNAL;
    goto done;
  }
  suite->source_path = confit_v2_compat_strdup(path);
  suite->source_hash =
      confit_v2_compat_hash_text(confit_v2_toml_document_source_text(document));
  status = suite->source_path != 0
               ? confit_v2_compat_read_string(name, &suite->name, diagnostic)
               : CONFIT_ERR_INTERNAL;
  if (status != CONFIT_OK || suite->name[0] == '\0') {
    if (status == CONFIT_OK) {
      status = CONFIT_ERR_SCHEMA;
      confit_v2_compat_diagnostic(diagnostic, status, name, kInvalidSource);
    }
    goto done;
  }
  for (index = 0U; index < confit_v2_toml_table_size(projects); ++index) {
    status = confit_v2_compat_append_alias(
        suite, confit_v2_toml_table_key_at(projects, index),
        confit_v2_toml_table_value_at(projects, index), diagnostic);
    if (status != CONFIT_OK) {
      goto done;
    }
  }
  if (suite->alias_count == 0U) {
    confit_v2_compat_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, projects,
                                kInvalidSource);
    status = CONFIT_ERR_SCHEMA;
    goto done;
  }
  for (index = 0U; index < confit_v2_toml_array_size(constraints); ++index) {
    status = confit_v2_compat_parse_constraint(
        suite, confit_v2_toml_array_at(constraints, index), diagnostic);
    if (status != CONFIT_OK) {
      goto done;
    }
  }
  if (suite->constraint_count == 0U) {
    confit_v2_compat_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, constraints,
                                kInvalidSource);
    status = CONFIT_ERR_SCHEMA;
    goto done;
  }
  qsort(suite->aliases, suite->alias_count, sizeof(*suite->aliases),
        confit_v2_compat_alias_compare);
  qsort(suite->constraints, suite->constraint_count, sizeof(*suite->constraints),
        confit_v2_compat_constraint_compare);
  for (index = 1U; index < suite->constraint_count; ++index) {
    if (strcmp(suite->constraints[index - 1U].id, suite->constraints[index].id) ==
        0) {
      confit_v2_compat_span_diagnostic(diagnostic, CONFIT_ERR_SCHEMA,
                                       &suite->constraints[index].span,
                                       kDuplicateId);
      status = CONFIT_ERR_SCHEMA;
      goto done;
    }
  }
  *out_suite = suite;
  suite = 0;
  status = CONFIT_OK;

done:
  confit_v2_compat_suite_free(suite);
  confit_v2_toml_document_free(document);
  return status;
}

static const char *confit_v2_compat_action_name(ConfitV2CompatAction action) {
  return action == CONFIT_V2_COMPAT_ACTION_FORBID ? "forbid" : "require";
}

static const char *confit_v2_compat_state_name(ConfitV2CompatResultState state) {
  switch (state) {
  case CONFIT_V2_COMPAT_RESULT_NOT_APPLICABLE:
    return "not_applicable";
  case CONFIT_V2_COMPAT_RESULT_PASS:
    return "pass";
  case CONFIT_V2_COMPAT_RESULT_FAIL:
    return "fail";
  default:
    return "unknown";
  }
}

static const char *confit_v2_compat_type_name(ConfitV2OptionType type) {
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

static const char *confit_v2_compat_origin_name(
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

static const char *confit_v2_compat_basename(const char *path) {
  const char *last = path;
  const char *cursor;

  if (path == 0) {
    return "";
  }
  for (cursor = path; *cursor != '\0'; ++cursor) {
    if (*cursor == '/' || *cursor == '\\') {
      last = cursor + 1;
    }
  }
  return last;
}

static void confit_v2_compat_trace_clear(ConfitV2CompatTrace *trace) {
  if (trace == 0) {
    return;
  }
  free(trace->alias);
  free(trace->option_id);
  free(trace->source_path);
  confit_v2_compat_value_clear(&trace->effective_value);
  memset(trace, 0, sizeof(*trace));
}

static void confit_v2_compat_report_result_clear(
    ConfitV2CompatReportResult *result) {
  size_t index;

  if (result == 0) {
    return;
  }
  free(result->id);
  free(result->when_text);
  free(result->condition_text);
  free(result->message);
  confit_v2_compat_span_clear(&result->span);
  for (index = 0U; index < result->trace_count; ++index) {
    confit_v2_compat_trace_clear(&result->traces[index]);
  }
  free(result->traces);
  memset(result, 0, sizeof(*result));
}

void confit_v2_compat_report_free(ConfitV2CompatReport *report) {
  size_t index;

  if (report == 0) {
    return;
  }
  for (index = 0U; index < report->project_count; ++index) {
    free(report->projects[index].alias);
    free(report->projects[index].name);
    free(report->projects[index].profile);
    free(report->projects[index].target);
  }
  for (index = 0U; index < report->result_count; ++index) {
    confit_v2_compat_report_result_clear(&report->results[index]);
  }
  free(report->projects);
  free(report->results);
  free(report->suite_name);
  free(report->source_path);
  free(report);
}

static int confit_v2_compat_binding_compare(const void *left, const void *right) {
  const ConfitV2CompatBinding *left_binding =
      (const ConfitV2CompatBinding *)left;
  const ConfitV2CompatBinding *right_binding =
      (const ConfitV2CompatBinding *)right;
  return strcmp(left_binding->internal_id, right_binding->internal_id);
}

static int confit_v2_compat_trace_compare(const void *left, const void *right) {
  const ConfitV2CompatTrace *left_trace = (const ConfitV2CompatTrace *)left;
  const ConfitV2CompatTrace *right_trace = (const ConfitV2CompatTrace *)right;
  int comparison = strcmp(left_trace->alias, right_trace->alias);
  return comparison != 0 ? comparison : strcmp(left_trace->option_id,
                                                 right_trace->option_id);
}

static void confit_v2_compat_bindings_free(ConfitV2CompatBinding *bindings,
                                           size_t count) {
  size_t index;

  for (index = 0U; index < count; ++index) {
    free(bindings[index].internal_id);
  }
  free(bindings);
}

static const ConfitV2CompatProject *confit_v2_compat_find_project(
    const ConfitV2CompatProject *projects, size_t project_count,
    const char *alias) {
  size_t index;

  for (index = 0U; index < project_count; ++index) {
    if (strcmp(projects[index].alias, alias) == 0) {
      return &projects[index];
    }
  }
  return 0;
}

static ConfitStatus confit_v2_compat_validate_projects(
    const ConfitV2CompatSuite *suite, const ConfitV2CompatProject *projects,
    size_t project_count, ConfitDiagnostic *diagnostic) {
  size_t index;
  size_t other;

  if (project_count != suite->alias_count) {
    confit_v2_compat_span_diagnostic(diagnostic, CONFIT_ERR_SCHEMA,
                                     &suite->aliases[0].span, kAliasMismatch);
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < project_count; ++index) {
    if (projects[index].alias == 0 || projects[index].snapshot == 0 ||
        !confit_v2_compat_alias_is_valid(projects[index].alias)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                            kInvalidArgument);
      return CONFIT_ERR_INVALID_ARGUMENT;
    }
    if (projects[index].schema_version != CONFIT_V2_COMPAT_SCHEMA_VERSION) {
      confit_v2_compat_span_diagnostic(diagnostic, CONFIT_ERR_SCHEMA,
                                       &suite->aliases[0].span, kMixedSchema);
      return CONFIT_ERR_SCHEMA;
    }
    if (projects[index].artifact_abi == 0 ||
        strcmp(projects[index].artifact_abi,
               CONFIT_V2_COMPAT_ARTIFACT_ABI) != 0) {
      confit_v2_compat_span_diagnostic(diagnostic, CONFIT_ERR_SCHEMA,
                                       &suite->aliases[0].span,
                                       kArtifactAbiMismatch);
      return CONFIT_ERR_SCHEMA;
    }
    if ((projects[index].expected_source_root != 0 &&
         strcmp(projects[index].expected_source_root,
                confit_v2_snapshot_source_root(projects[index].snapshot)) != 0) ||
        (projects[index].expected_source_hash != 0U &&
         projects[index].expected_source_hash !=
             confit_v2_snapshot_source_hash(projects[index].snapshot)) ||
        (projects[index].expected_snapshot_hash != 0U &&
         projects[index].expected_snapshot_hash !=
             confit_v2_snapshot_semantic_hash(projects[index].snapshot))) {
      confit_v2_compat_span_diagnostic(diagnostic, CONFIT_ERR_SCHEMA,
                                       &suite->aliases[0].span,
                                       kSnapshotIdentityMismatch);
      return CONFIT_ERR_SCHEMA;
    }
    if (confit_v2_compat_find_project(projects, project_count,
                                      suite->aliases[index].name) == 0) {
      confit_v2_compat_span_diagnostic(diagnostic, CONFIT_ERR_SCHEMA,
                                       &suite->aliases[index].span,
                                       kAliasMismatch);
      return CONFIT_ERR_SCHEMA;
    }
    for (other = index + 1U; other < project_count; ++other) {
      if (strcmp(projects[index].alias, projects[other].alias) == 0) {
        confit_v2_compat_span_diagnostic(diagnostic, CONFIT_ERR_SCHEMA,
                                         &suite->aliases[0].span,
                                         kAliasMismatch);
        return CONFIT_ERR_SCHEMA;
      }
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_compat_build_bindings(
    const ConfitV2CompatSuite *suite, const ConfitV2CompatProject *projects,
    size_t project_count, ConfitV2CompatBinding **out_bindings,
    size_t *out_count, ConfitDiagnostic *diagnostic) {
  ConfitV2CompatBinding *bindings;
  size_t count = 0U;
  size_t alias_index;
  size_t option_index;
  size_t cursor = 0U;

  *out_bindings = 0;
  *out_count = 0U;
  for (alias_index = 0U; alias_index < suite->alias_count; ++alias_index) {
    const ConfitV2CompatProject *project = confit_v2_compat_find_project(
        projects, project_count, suite->aliases[alias_index].name);
    const size_t option_count = confit_v2_snapshot_option_count(project->snapshot);
    if (option_count > SIZE_MAX - count) {
      return CONFIT_ERR_INTERNAL;
    }
    count += option_count;
  }
  bindings = count == 0U ? 0 : (ConfitV2CompatBinding *)calloc(count, sizeof(*bindings));
  if (count > 0U && bindings == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, suite->source_path, 0U,
                          0U, kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  for (alias_index = 0U; alias_index < suite->alias_count; ++alias_index) {
    const ConfitV2CompatProject *project = confit_v2_compat_find_project(
        projects, project_count, suite->aliases[alias_index].name);
    for (option_index = 0U;
         option_index < confit_v2_snapshot_option_count(project->snapshot);
         ++option_index) {
      const ConfitV2SnapshotOption *option =
          confit_v2_snapshot_option_at(project->snapshot, option_index);
      const size_t alias_size = strlen(project->alias);
      const size_t option_size = strlen(option->id);
      ConfitV2CompatBinding *binding = &bindings[cursor++];

      binding->internal_id = (char *)malloc(alias_size + 2U + option_size + 1U);
      if (binding->internal_id == 0) {
        confit_v2_compat_bindings_free(bindings, count);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, suite->source_path,
                              0U, 0U, kAllocationFailed);
        return CONFIT_ERR_INTERNAL;
      }
      (void)snprintf(binding->internal_id, alias_size + 2U + option_size + 1U,
                     "%s__%s", project->alias, option->id);
      binding->alias = project->alias;
      binding->option = option;
      binding->binding.id = binding->internal_id;
      binding->binding.type = confit_v2_expression_type_from_option_type(
          option->type, binding->internal_id);
      binding->binding.value = option->effective_is_set ? &option->effective_value : 0;
    }
  }
  qsort(bindings, count, sizeof(*bindings), confit_v2_compat_binding_compare);
  *out_bindings = bindings;
  *out_count = count;
  return CONFIT_OK;
}

static const ConfitV2CompatBinding *confit_v2_compat_find_binding(
    const ConfitV2CompatBinding *bindings, size_t count, const char *id) {
  size_t low = 0U;
  size_t high = count;

  while (low < high) {
    const size_t middle = low + (high - low) / 2U;
    const int comparison = strcmp(bindings[middle].internal_id, id);
    if (comparison == 0) {
      return &bindings[middle];
    }
    if (comparison < 0) {
      low = middle + 1U;
    } else {
      high = middle;
    }
  }
  return 0;
}

static char *confit_v2_compat_rewrite_expression(const char *text) {
  char *copy;
  size_t index;
  char quote = '\0';
  int escaped = 0;

  copy = confit_v2_compat_strdup(text);
  if (copy == 0) {
    return 0;
  }
  for (index = 0U; copy[index] != '\0'; ++index) {
    const char value = copy[index];
    if (quote != '\0') {
      if (quote == '"' && !escaped && value == '\\') {
        escaped = 1;
      } else if (!escaped && value == quote) {
        quote = '\0';
      } else {
        escaped = 0;
      }
      continue;
    }
    if (value == '\'' || value == '"') {
      quote = value;
    } else if (value == ':' && copy[index + 1U] == ':') {
      copy[index] = '_';
      copy[index + 1U] = '_';
      ++index;
    }
  }
  return copy;
}

static ConfitStatus confit_v2_compat_append_trace(
    ConfitV2CompatReportResult *result, const ConfitV2CompatBinding *binding) {
  ConfitV2CompatTrace *grown;
  ConfitV2CompatTrace *trace;
  size_t index;
  ConfitStatus status;

  for (index = 0U; index < result->trace_count; ++index) {
    if (strcmp(result->traces[index].alias, binding->alias) == 0 &&
        strcmp(result->traces[index].option_id, binding->option->id) == 0) {
      return CONFIT_OK;
    }
  }
  grown = (ConfitV2CompatTrace *)realloc(
      result->traces, (result->trace_count + 1U) * sizeof(*result->traces));
  if (grown == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  result->traces = grown;
  trace = &result->traces[result->trace_count];
  memset(trace, 0, sizeof(*trace));
  trace->alias = confit_v2_compat_strdup(binding->alias);
  trace->option_id = confit_v2_compat_strdup(binding->option->id);
  trace->source_path =
      confit_v2_compat_strdup(binding->option->effective_source_path);
  if (trace->alias == 0 || trace->option_id == 0 ||
      (binding->option->effective_source_path != 0 && trace->source_path == 0)) {
    confit_v2_compat_trace_clear(trace);
    return CONFIT_ERR_INTERNAL;
  }
  trace->type = binding->option->type;
  trace->effective_is_set = binding->option->effective_is_set;
  trace->effective_origin = binding->option->effective_origin;
  trace->source_line = binding->option->effective_source_line;
  trace->source_column = binding->option->effective_source_column;
  status = trace->effective_is_set
               ? confit_v2_compat_value_copy(&trace->effective_value,
                                              &binding->option->effective_value)
               : CONFIT_OK;
  if (status != CONFIT_OK) {
    confit_v2_compat_trace_clear(trace);
    return status;
  }
  result->trace_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_compat_collect_references(
    const ConfitV2ExpressionNode *node, const ConfitV2CompatBinding *bindings,
    size_t binding_count, ConfitV2CompatReportResult *result) {
  size_t index;
  ConfitStatus status;

  if (node == 0) {
    return CONFIT_OK;
  }
  if (node->kind == CONFIT_V2_EXPRESSION_NODE_REFERENCE) {
    const ConfitV2CompatBinding *binding = confit_v2_compat_find_binding(
        bindings, binding_count, node->as.reference.option_id);
    return binding != 0 ? confit_v2_compat_append_trace(result, binding)
                        : CONFIT_OK;
  }
  if (node->kind == CONFIT_V2_EXPRESSION_NODE_UNARY) {
    return confit_v2_compat_collect_references(node->as.unary.operand, bindings,
                                               binding_count, result);
  }
  if (node->kind == CONFIT_V2_EXPRESSION_NODE_BINARY) {
    status = confit_v2_compat_collect_references(node->as.binary.left, bindings,
                                                  binding_count, result);
    return status == CONFIT_OK
               ? confit_v2_compat_collect_references(node->as.binary.right,
                                                     bindings, binding_count,
                                                     result)
               : status;
  }
  if (node->kind == CONFIT_V2_EXPRESSION_NODE_CONDITIONAL) {
    status = confit_v2_compat_collect_references(node->as.conditional.condition,
                                                  bindings, binding_count, result);
    if (status == CONFIT_OK) {
      status = confit_v2_compat_collect_references(node->as.conditional.when_true,
                                                    bindings, binding_count,
                                                    result);
    }
    return status == CONFIT_OK
               ? confit_v2_compat_collect_references(node->as.conditional.when_false,
                                                     bindings, binding_count,
                                                     result)
               : status;
  }
  if (node->kind == CONFIT_V2_EXPRESSION_NODE_CALL) {
    for (index = 0U; index < node->as.call.argument_count; ++index) {
      status = confit_v2_compat_collect_references(node->as.call.arguments[index],
                                                    bindings, binding_count,
                                                    result);
      if (status != CONFIT_OK) {
        return status;
      }
    }
  } else if (node->kind == CONFIT_V2_EXPRESSION_NODE_LIST) {
    for (index = 0U; index < node->as.list.item_count; ++index) {
      status = confit_v2_compat_collect_references(node->as.list.items[index],
                                                    bindings, binding_count,
                                                    result);
      if (status != CONFIT_OK) {
        return status;
      }
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_compat_evaluate_expression(
    const ConfitV2CompatExpressionSource *source,
    const ConfitV2ExpressionEnvironment *environment,
    const ConfitV2CompatBinding *bindings, size_t binding_count,
    ConfitV2CompatReportResult *result, int *out_bool,
    ConfitDiagnostic *diagnostic) {
  ConfitV2ExpressionText text;
  ConfitV2Expression *expression = 0;
  ConfitV2TypedExpression *typed = 0;
  ConfitV2ExpressionValue value;
  char *rewritten = 0;
  ConfitStatus status;

  *out_bool = 0;
  memset(&text, 0, sizeof(text));
  memset(&value, 0, sizeof(value));
  rewritten = confit_v2_compat_rewrite_expression(source->text);
  if (rewritten == 0) {
    confit_v2_compat_span_diagnostic(diagnostic, CONFIT_ERR_INTERNAL,
                                     &source->span, kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  text.text = rewritten;
  text.span.path = source->span.path;
  text.span.line = source->span.line;
  text.span.column = source->span.column;
  status = confit_v2_expression_parse(&text, 0, &expression, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_expression_type_check(expression, environment, &typed,
                                              diagnostic);
  }
  if (status == CONFIT_OK && typed->root_type.kind != CONFIT_V2_EXPRESSION_TYPE_BOOL) {
    confit_v2_compat_span_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, &source->span,
                                     kBooleanRequired);
    status = CONFIT_ERR_SCHEMA;
  }
  if (status == CONFIT_OK) {
    status = confit_v2_expression_evaluate(typed, environment, &value, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_compat_collect_references(expression->root, bindings,
                                                  binding_count, result);
  }
  if (status == CONFIT_OK) {
    *out_bool = value.is_set && value.value.kind == CONFIT_V2_VALUE_BOOL &&
                value.value.as.bool_value != 0;
    if (!value.is_set || value.value.kind != CONFIT_V2_VALUE_BOOL) {
      confit_v2_compat_span_diagnostic(diagnostic, CONFIT_ERR_SCHEMA, &source->span,
                                       kBooleanRequired);
      status = CONFIT_ERR_SCHEMA;
    }
  }
  confit_v2_expression_value_clear(&value);
  confit_v2_typed_expression_free(typed);
  confit_v2_expression_free(expression);
  free(rewritten);
  return status;
}

static ConfitStatus confit_v2_compat_copy_span(ConfitV2CompatSpan *out,
                                                const ConfitV2CompatSpan *source) {
  memset(out, 0, sizeof(*out));
  out->path = confit_v2_compat_strdup(source->path);
  if (out->path == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  out->line = source->line;
  out->column = source->column;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_compat_copy_report_project(
    ConfitV2CompatReportProject *out, const ConfitV2CompatProject *project) {
  const ConfitV2Snapshot *snapshot = project->snapshot;

  memset(out, 0, sizeof(*out));
  out->alias = confit_v2_compat_strdup(project->alias);
  out->name = confit_v2_compat_strdup(
      confit_v2_snapshot_project_name(snapshot) != 0
          ? confit_v2_snapshot_project_name(snapshot)
          : "");
  out->profile = confit_v2_compat_strdup(
      confit_v2_snapshot_profile_name(snapshot) != 0
          ? confit_v2_snapshot_profile_name(snapshot)
          : "");
  out->target = confit_v2_compat_strdup(
      confit_v2_snapshot_target_name(snapshot) != 0
          ? confit_v2_snapshot_target_name(snapshot)
          : "");
  if (out->alias == 0 || out->name == 0 || out->profile == 0 || out->target == 0) {
    free(out->alias);
    free(out->name);
    free(out->profile);
    free(out->target);
    memset(out, 0, sizeof(*out));
    return CONFIT_ERR_INTERNAL;
  }
  out->source_hash = confit_v2_snapshot_source_hash(snapshot);
  out->input_hash = confit_v2_snapshot_input_hash(snapshot);
  out->snapshot_hash = confit_v2_snapshot_semantic_hash(snapshot);
  return CONFIT_OK;
}

ConfitStatus confit_v2_compat_check(
    const ConfitV2CompatSuite *suite, const ConfitV2CompatProject *projects,
    size_t project_count, ConfitV2CompatReport **out_report,
    ConfitDiagnostic *diagnostic) {
  ConfitV2CompatBinding *bindings = 0;
  ConfitV2ExpressionBinding *expression_bindings = 0;
  ConfitV2ExpressionEnvironment environment;
  ConfitV2CompatReport *report = 0;
  size_t binding_count = 0U;
  size_t index;
  ConfitStatus status;

  if (out_report == 0 || suite == 0 || projects == 0 || project_count == 0U) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0U, 0U,
                          kInvalidArgument);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_report = 0;
  status = confit_v2_compat_validate_projects(suite, projects, project_count,
                                               diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_v2_compat_build_bindings(suite, projects, project_count,
                                            &bindings, &binding_count,
                                            diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  expression_bindings = binding_count == 0U
                            ? 0
                            : (ConfitV2ExpressionBinding *)calloc(
                                  binding_count, sizeof(*expression_bindings));
  if (binding_count > 0U && expression_bindings == 0) {
    confit_v2_compat_bindings_free(bindings, binding_count);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, suite->source_path, 0U,
                          0U, kAllocationFailed);
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < binding_count; ++index) {
    expression_bindings[index] = bindings[index].binding;
  }
  memset(&environment, 0, sizeof(environment));
  environment.bindings = expression_bindings;
  environment.binding_count = binding_count;
  report = (ConfitV2CompatReport *)calloc(1U, sizeof(*report));
  if (report == 0) {
    status = CONFIT_ERR_INTERNAL;
    confit_diagnostic_set(diagnostic, status, suite->source_path, 0U, 0U,
                          kAllocationFailed);
    goto done;
  }
  report->suite_name = confit_v2_compat_strdup(suite->name);
  report->source_path = confit_v2_compat_strdup(suite->source_path);
  report->projects = (ConfitV2CompatReportProject *)calloc(
      suite->alias_count, sizeof(*report->projects));
  report->results = (ConfitV2CompatReportResult *)calloc(
      suite->constraint_count, sizeof(*report->results));
  if (report->suite_name == 0 || report->source_path == 0 || report->projects == 0 ||
      report->results == 0) {
    status = CONFIT_ERR_INTERNAL;
    confit_diagnostic_set(diagnostic, status, suite->source_path, 0U, 0U,
                          kAllocationFailed);
    goto done;
  }
  report->project_count = suite->alias_count;
  report->result_count = suite->constraint_count;
  for (index = 0U; index < suite->alias_count; ++index) {
    const ConfitV2CompatProject *project = confit_v2_compat_find_project(
        projects, project_count, suite->aliases[index].name);
    status = confit_v2_compat_copy_report_project(&report->projects[index], project);
    if (status != CONFIT_OK) {
      confit_diagnostic_set(diagnostic, status, suite->source_path, 0U, 0U,
                            kAllocationFailed);
      goto done;
    }
  }
  for (index = 0U; index < suite->constraint_count; ++index) {
    const ConfitV2CompatConstraint *constraint = &suite->constraints[index];
    ConfitV2CompatReportResult *result = &report->results[index];
    int when_result;
    int condition_result;

    result->id = confit_v2_compat_strdup(constraint->id);
    result->when_text = confit_v2_compat_strdup(constraint->when.text);
    result->condition_text = confit_v2_compat_strdup(constraint->condition.text);
    result->message = confit_v2_compat_strdup(constraint->message);
    result->action = constraint->action;
    status = result->id != 0 && result->when_text != 0 &&
                     result->condition_text != 0 && result->message != 0
                 ? confit_v2_compat_copy_span(&result->span, &constraint->span)
                 : CONFIT_ERR_INTERNAL;
    if (status != CONFIT_OK) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, suite->source_path,
                            0U, 0U, kAllocationFailed);
      goto done;
    }
    status = confit_v2_compat_evaluate_expression(
        &constraint->when, &environment, bindings, binding_count, result,
        &when_result, diagnostic);
    if (status == CONFIT_OK && !when_result) {
      result->state = CONFIT_V2_COMPAT_RESULT_NOT_APPLICABLE;
      report->not_applicable_count += 1U;
      continue;
    }
    if (status == CONFIT_OK) {
      status = confit_v2_compat_evaluate_expression(
          &constraint->condition, &environment, bindings, binding_count, result,
          &condition_result, diagnostic);
    }
    if (status != CONFIT_OK) {
      goto done;
    }
    result->state = (constraint->action == CONFIT_V2_COMPAT_ACTION_REQUIRE
                         ? condition_result
                         : !condition_result)
                        ? CONFIT_V2_COMPAT_RESULT_PASS
                        : CONFIT_V2_COMPAT_RESULT_FAIL;
    if (result->trace_count > 1U) {
      qsort(result->traces, result->trace_count, sizeof(*result->traces),
            confit_v2_compat_trace_compare);
    }
    if (result->state == CONFIT_V2_COMPAT_RESULT_PASS) {
      report->pass_count += 1U;
    } else {
      report->fail_count += 1U;
    }
  }
  report->source_hash = suite->source_hash;
  *out_report = report;
  report = 0;
  if ((*out_report)->fail_count > 0U) {
    const ConfitV2CompatReportResult *failure = 0;
    for (index = 0U; index < (*out_report)->result_count; ++index) {
      if ((*out_report)->results[index].state == CONFIT_V2_COMPAT_RESULT_FAIL) {
        failure = &(*out_report)->results[index];
        break;
      }
    }
    confit_v2_compat_span_diagnostic(diagnostic, CONFIT_ERR_COMPATIBILITY,
                                     &failure->span, failure->message);
    status = CONFIT_ERR_COMPATIBILITY;
  } else {
    status = CONFIT_OK;
  }

done:
  confit_v2_compat_report_free(report);
  free(expression_bindings);
  confit_v2_compat_bindings_free(bindings, binding_count);
  return status;
}

static void confit_v2_compat_builder_init(ConfitV2CompatBuilder *builder) {
  memset(builder, 0, sizeof(*builder));
}

static void confit_v2_compat_builder_clear(ConfitV2CompatBuilder *builder) {
  free(builder->text);
  memset(builder, 0, sizeof(*builder));
}

static ConfitStatus confit_v2_compat_builder_reserve(ConfitV2CompatBuilder *builder,
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
  capacity = builder->capacity == 0U ? 256U : builder->capacity;
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

static ConfitStatus confit_v2_compat_builder_append_n(
    ConfitV2CompatBuilder *builder, const char *text, size_t size) {
  ConfitStatus status = confit_v2_compat_builder_reserve(builder, size);

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

static ConfitStatus confit_v2_compat_builder_append(ConfitV2CompatBuilder *builder,
                                                     const char *text) {
  return confit_v2_compat_builder_append_n(builder, text, strlen(text));
}

static ConfitStatus confit_v2_compat_builder_appendf(ConfitV2CompatBuilder *builder,
                                                      const char *format, ...) {
  va_list arguments;
  va_list copied;
  int size;
  ConfitStatus status;

  va_start(arguments, format);
  va_copy(copied, arguments);
  size = vsnprintf(0, 0U, format, copied);
  va_end(copied);
  if (size < 0) {
    va_end(arguments);
    return CONFIT_ERR_INTERNAL;
  }
  status = confit_v2_compat_builder_reserve(builder, (size_t)size);
  if (status == CONFIT_OK) {
    (void)vsnprintf(builder->text + builder->size,
                    builder->capacity - builder->size, format, arguments);
    builder->size += (size_t)size;
  }
  va_end(arguments);
  return status;
}

static char *confit_v2_compat_builder_take(ConfitV2CompatBuilder *builder) {
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

static ConfitStatus confit_v2_compat_builder_json_string(
    ConfitV2CompatBuilder *builder, const char *text) {
  const unsigned char *cursor = (const unsigned char *)(text != 0 ? text : "");
  ConfitStatus status = confit_v2_compat_builder_append(builder, "\"");

  while (status == CONFIT_OK && *cursor != '\0') {
    if (*cursor == '"' || *cursor == '\\') {
      status = confit_v2_compat_builder_appendf(builder, "\\%c", *cursor);
    } else if (*cursor < 0x20U) {
      status = confit_v2_compat_builder_appendf(builder, "\\u%04x", *cursor);
    } else {
      status = confit_v2_compat_builder_append_n(builder, (const char *)cursor, 1U);
    }
    ++cursor;
  }
  return status == CONFIT_OK ? confit_v2_compat_builder_append(builder, "\"")
                             : status;
}

static ConfitStatus confit_v2_compat_builder_value_json(
    ConfitV2CompatBuilder *builder, const ConfitV2Value *value) {
  size_t index;
  ConfitStatus status;

  if (value == 0 || value->kind == CONFIT_V2_VALUE_UNSET) {
    return confit_v2_compat_builder_append(builder, "null");
  }
  switch (value->kind) {
  case CONFIT_V2_VALUE_BOOL:
    return confit_v2_compat_builder_append(builder,
                                           value->as.bool_value ? "true" : "false");
  case CONFIT_V2_VALUE_TRISTATE:
    return confit_v2_compat_builder_appendf(builder, "\"%c\"",
                                             value->as.tristate_value);
  case CONFIT_V2_VALUE_INT:
    return confit_v2_compat_builder_appendf(builder, "%lld",
                                             (long long)value->as.int_value);
  case CONFIT_V2_VALUE_UINT:
    return confit_v2_compat_builder_appendf(builder, "%llu",
                                             (unsigned long long)value->as.uint_value);
  case CONFIT_V2_VALUE_FLOAT:
    return confit_v2_compat_builder_appendf(builder, "%.17g", value->as.float_value);
  case CONFIT_V2_VALUE_STRING:
    return confit_v2_compat_builder_json_string(builder, value->as.string_value);
  case CONFIT_V2_VALUE_STRING_LIST:
    status = confit_v2_compat_builder_append(builder, "[");
    for (index = 0U; status == CONFIT_OK && index < value->as.string_list.count;
         ++index) {
      if (index > 0U) {
        status = confit_v2_compat_builder_append(builder, ", ");
      }
      if (status == CONFIT_OK) {
        status = confit_v2_compat_builder_json_string(
            builder, value->as.string_list.items[index]);
      }
    }
    return status == CONFIT_OK ? confit_v2_compat_builder_append(builder, "]")
                               : status;
  case CONFIT_V2_VALUE_UNSET:
  default:
    return confit_v2_compat_builder_append(builder, "null");
  }
}

static ConfitStatus confit_v2_compat_builder_trace_json(
    ConfitV2CompatBuilder *builder, const ConfitV2CompatTrace *trace) {
  ConfitStatus status;

  status = confit_v2_compat_builder_append(builder, "{\"alias\": ");
  if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(builder, trace->alias);
  if (status == CONFIT_OK) status = confit_v2_compat_builder_append(builder, ", \"option\": ");
  if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(builder, trace->option_id);
  if (status == CONFIT_OK) status = confit_v2_compat_builder_append(builder, ", \"type\": ");
  if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(builder, confit_v2_compat_type_name(trace->type));
  if (status == CONFIT_OK) status = confit_v2_compat_builder_append(builder, ", \"is_set\": ");
  if (status == CONFIT_OK) status = confit_v2_compat_builder_append(builder, trace->effective_is_set ? "true" : "false");
  if (status == CONFIT_OK) status = confit_v2_compat_builder_append(builder, ", \"value\": ");
  if (status == CONFIT_OK) status = confit_v2_compat_builder_value_json(builder, &trace->effective_value);
  if (status == CONFIT_OK) status = confit_v2_compat_builder_append(builder, ", \"provenance\": ");
  if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(builder, confit_v2_compat_origin_name(trace->effective_origin));
  if (status == CONFIT_OK) status = confit_v2_compat_builder_append(builder, ", \"source\": {\"path\": ");
  if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(builder, confit_v2_compat_basename(trace->source_path));
  if (status == CONFIT_OK) status = confit_v2_compat_builder_appendf(builder, ", \"line\": %llu, \"column\": %llu}}",
                                                             (unsigned long long)trace->source_line,
                                                             (unsigned long long)trace->source_column);
  return status;
}

ConfitStatus confit_v2_compat_report_to_json(const ConfitV2CompatReport *report,
                                              char **out_json) {
  ConfitV2CompatBuilder builder;
  size_t index;
  ConfitStatus status;

  if (report == 0 || out_json == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_json = 0;
  confit_v2_compat_builder_init(&builder);
  status = confit_v2_compat_builder_append(&builder,
      "{\n  \"schema\": \"confit-compat-report-v2\",\n  \"suite\": ");
  if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(&builder, report->suite_name);
  if (status == CONFIT_OK) status = confit_v2_compat_builder_appendf(
      &builder, ",\n  \"source_hash\": \"%016llx\",\n  \"projects\": [\n",
      (unsigned long long)report->source_hash);
  for (index = 0U; status == CONFIT_OK && index < report->project_count; ++index) {
    const ConfitV2CompatReportProject *project = &report->projects[index];
    if (index > 0U) status = confit_v2_compat_builder_append(&builder, ",\n");
    if (status == CONFIT_OK) status = confit_v2_compat_builder_append(&builder, "    {\"alias\": ");
    if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(&builder, project->alias);
    if (status == CONFIT_OK) status = confit_v2_compat_builder_append(&builder, ", \"project\": ");
    if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(&builder, project->name);
    if (status == CONFIT_OK) status = confit_v2_compat_builder_append(&builder, ", \"profile\": ");
    if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(&builder, project->profile);
    if (status == CONFIT_OK) status = confit_v2_compat_builder_append(&builder, ", \"target\": ");
    if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(&builder, project->target);
    if (status == CONFIT_OK) status = confit_v2_compat_builder_appendf(&builder, ", \"source_hash\": \"%016llx\", \"input_hash\": \"%016llx\", \"snapshot_hash\": \"%016llx\"}",
                                                       (unsigned long long)project->source_hash,
                                                       (unsigned long long)project->input_hash,
                                                       (unsigned long long)project->snapshot_hash);
  }
  if (status == CONFIT_OK) status = confit_v2_compat_builder_append(&builder, "\n  ],\n  \"constraints\": [\n");
  for (index = 0U; status == CONFIT_OK && index < report->result_count; ++index) {
    const ConfitV2CompatReportResult *result = &report->results[index];
    size_t trace_index;
    if (index > 0U) status = confit_v2_compat_builder_append(&builder, ",\n");
    if (status == CONFIT_OK) status = confit_v2_compat_builder_append(&builder, "    {\"id\": ");
    if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(&builder, result->id);
    if (status == CONFIT_OK) status = confit_v2_compat_builder_append(&builder, ", \"state\": ");
    if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(&builder, confit_v2_compat_state_name(result->state));
    if (status == CONFIT_OK) status = confit_v2_compat_builder_append(&builder, ", \"action\": ");
    if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(&builder, confit_v2_compat_action_name(result->action));
    if (status == CONFIT_OK) status = confit_v2_compat_builder_append(&builder, ", \"message\": ");
    if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(&builder, result->message);
    if (status == CONFIT_OK) status = confit_v2_compat_builder_append(&builder, ", \"source\": {\"path\": ");
    if (status == CONFIT_OK) status = confit_v2_compat_builder_json_string(&builder, confit_v2_compat_basename(result->span.path));
    if (status == CONFIT_OK) status = confit_v2_compat_builder_appendf(&builder, ", \"line\": %llu, \"column\": %llu}, \"causal_values\": [",
                                                       (unsigned long long)result->span.line,
                                                       (unsigned long long)result->span.column);
    for (trace_index = 0U; status == CONFIT_OK && trace_index < result->trace_count;
         ++trace_index) {
      if (trace_index > 0U) status = confit_v2_compat_builder_append(&builder, ", ");
      if (status == CONFIT_OK) status = confit_v2_compat_builder_trace_json(&builder, &result->traces[trace_index]);
    }
    if (status == CONFIT_OK) status = confit_v2_compat_builder_append(&builder, "]}");
  }
  if (status == CONFIT_OK) status = confit_v2_compat_builder_appendf(&builder,
      "\n  ],\n  \"summary\": {\"constraints\": %llu, \"passed\": %llu, \"failed\": %llu, \"not_applicable\": %llu}\n}\n",
      (unsigned long long)report->result_count, (unsigned long long)report->pass_count,
      (unsigned long long)report->fail_count,
      (unsigned long long)report->not_applicable_count);
  if (status == CONFIT_OK) {
    *out_json = confit_v2_compat_builder_take(&builder);
    status = *out_json != 0 ? CONFIT_OK : CONFIT_ERR_INTERNAL;
  }
  confit_v2_compat_builder_clear(&builder);
  return status;
}

ConfitStatus confit_v2_compat_report_to_text(const ConfitV2CompatReport *report,
                                              char **out_text) {
  ConfitV2CompatBuilder builder;
  size_t index;
  ConfitStatus status;

  if (report == 0 || out_text == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_text = 0;
  confit_v2_compat_builder_init(&builder);
  status = confit_v2_compat_builder_appendf(&builder, "compat %s\n", report->suite_name);
  for (index = 0U; status == CONFIT_OK && index < report->result_count; ++index) {
    const ConfitV2CompatReportResult *result = &report->results[index];
    size_t trace_index;
    status = confit_v2_compat_builder_appendf(
        &builder, "%s: %s (%s)\n", result->id,
        confit_v2_compat_state_name(result->state), result->message);
    for (trace_index = 0U; status == CONFIT_OK && trace_index < result->trace_count;
         ++trace_index) {
      const ConfitV2CompatTrace *trace = &result->traces[trace_index];
      status = confit_v2_compat_builder_appendf(
          &builder, "  %s::%s [%s, %s] @ %s:%llu\n", trace->alias,
          trace->option_id, confit_v2_compat_type_name(trace->type),
          confit_v2_compat_origin_name(trace->effective_origin),
          confit_v2_compat_basename(trace->source_path),
          (unsigned long long)trace->source_line);
    }
  }
  if (status == CONFIT_OK) {
    *out_text = confit_v2_compat_builder_take(&builder);
    status = *out_text != 0 ? CONFIT_OK : CONFIT_ERR_INTERNAL;
  }
  confit_v2_compat_builder_clear(&builder);
  return status;
}

uint64_t confit_v2_compat_report_semantic_hash(const ConfitV2CompatReport *report) {
  char *json = 0;
  uint64_t hash = UINT64_C(1469598103934665603);
  size_t index;

  if (confit_v2_compat_report_to_json(report, &json) != CONFIT_OK) {
    return 0U;
  }
  for (index = 0U; json[index] != '\0'; ++index) {
    hash ^= (unsigned char)json[index];
    hash *= UINT64_C(1099511628211);
  }
  free(json);
  return hash;
}

void confit_v2_compat_string_free(char *text) { free(text); }
