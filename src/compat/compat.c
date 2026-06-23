#include "confit/compat.h"

#include <stdlib.h>
#include <string.h>

#include "confit/host.h"
#include "confit/parser.h"

typedef enum ConfitCompatTableKind {
  CONFIT_COMPAT_TABLE_NONE = 0,
  CONFIT_COMPAT_TABLE_COMPAT = 1,
  CONFIT_COMPAT_TABLE_ASSERT = 2,
} ConfitCompatTableKind;

static char *confit_compat_copy_string(const char *text) {
  char *copy;
  size_t size;

  if (text == 0) {
    return 0;
  }

  size = strlen(text);
  copy = (char *)malloc(size + 1U);
  if (copy == 0) {
    return 0;
  }
  memcpy(copy, text, size + 1U);
  return copy;
}

static ConfitStatus confit_compat_replace_string(char **slot,
                                                 const char *text) {
  char *copy;

  copy = confit_compat_copy_string(text);
  if (text != 0 && copy == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  free(*slot);
  *slot = copy;
  return CONFIT_OK;
}

static void confit_compat_condition_init(ConfitCompatCondition *condition) {
  condition->option_id = 0;
  confit_value_init(&condition->equals);
}

static void confit_compat_condition_clear(ConfitCompatCondition *condition) {
  if (condition == 0) {
    return;
  }

  free(condition->option_id);
  confit_value_clear(&condition->equals);
  confit_compat_condition_init(condition);
}

static void confit_compat_assertion_init(ConfitCompatAssertion *assertion) {
  confit_compat_condition_init(&assertion->when);
  assertion->action = CONFIT_COMPAT_REQUIRES;
  confit_compat_condition_init(&assertion->condition);
  assertion->message = 0;
  assertion->source = 0;
}

static void confit_compat_assertion_clear(ConfitCompatAssertion *assertion) {
  if (assertion == 0) {
    return;
  }

  confit_compat_condition_clear(&assertion->when);
  confit_compat_condition_clear(&assertion->condition);
  free(assertion->message);
  free(assertion->source);
  confit_compat_assertion_init(assertion);
}

static ConfitCompatSuite *confit_compat_suite_create(void) {
  return (ConfitCompatSuite *)calloc(1U, sizeof(ConfitCompatSuite));
}

void confit_compat_suite_free(ConfitCompatSuite *suite) {
  size_t index;

  if (suite == 0) {
    return;
  }

  for (index = 0U; index < suite->assertion_count; ++index) {
    confit_compat_assertion_clear(&suite->assertions[index]);
  }
  free(suite->assertions);
  free(suite->name);
  free(suite);
}

static ConfitCompatAssertion *confit_compat_add_assertion(
    ConfitCompatSuite *suite) {
  ConfitCompatAssertion *new_assertions;

  new_assertions =
      (ConfitCompatAssertion *)realloc(
          suite->assertions,
          (suite->assertion_count + 1U) * sizeof(suite->assertions[0]));
  if (new_assertions == 0) {
    return 0;
  }

  suite->assertions = new_assertions;
  confit_compat_assertion_init(&suite->assertions[suite->assertion_count]);
  suite->assertion_count += 1U;
  return &suite->assertions[suite->assertion_count - 1U];
}

static size_t confit_compat_trim_left(const char *text, size_t begin,
                                      size_t end) {
  while (begin < end &&
         (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
    begin += 1U;
  }
  return begin;
}

static size_t confit_compat_trim_right(const char *text, size_t begin,
                                       size_t end) {
  while (end > begin &&
         (text[end - 1U] == ' ' || text[end - 1U] == '\t' ||
          text[end - 1U] == '\r')) {
    end -= 1U;
  }
  return end;
}

static size_t confit_compat_strip_comment(const char *text, size_t begin,
                                          size_t end) {
  size_t index;
  int in_string;
  char quote;

  in_string = 0;
  quote = '\0';
  for (index = begin; index < end; ++index) {
    if (in_string) {
      if (text[index] == '\\' && index + 1U < end) {
        index += 1U;
        continue;
      }
      if (text[index] == quote) {
        in_string = 0;
      }
      continue;
    }

    if (text[index] == '"' || text[index] == '\'') {
      in_string = 1;
      quote = text[index];
      continue;
    }
    if (text[index] == '#') {
      return index;
    }
  }
  return end;
}

static char *confit_compat_parse_bare_key(const char *text, size_t begin,
                                          size_t end) {
  char *key;
  size_t size;

  begin = confit_compat_trim_left(text, begin, end);
  end = confit_compat_trim_right(text, begin, end);
  if (begin >= end) {
    return 0;
  }

  size = end - begin;
  key = (char *)malloc(size + 1U);
  if (key == 0) {
    return 0;
  }
  memcpy(key, text + begin, size);
  key[size] = '\0';
  return key;
}

static char *confit_compat_parse_quoted_string(const char *text, size_t begin,
                                               size_t end) {
  char *out;
  size_t write_index;
  size_t index;
  char quote;

  begin = confit_compat_trim_left(text, begin, end);
  end = confit_compat_trim_right(text, begin, end);
  if (begin + 1U >= end || (text[begin] != '"' && text[begin] != '\'')) {
    return 0;
  }
  quote = text[begin];
  if (text[end - 1U] != quote) {
    return 0;
  }

  out = (char *)malloc(end - begin);
  if (out == 0) {
    return 0;
  }

  write_index = 0U;
  for (index = begin + 1U; index + 1U < end; ++index) {
    if (text[index] == '\\' && index + 1U < end) {
      index += 1U;
      switch (text[index]) {
      case 'n':
        out[write_index] = '\n';
        break;
      case 'r':
        out[write_index] = '\r';
        break;
      case 't':
        out[write_index] = '\t';
        break;
      default:
        out[write_index] = text[index];
        break;
      }
    } else {
      out[write_index] = text[index];
    }
    write_index += 1U;
  }
  out[write_index] = '\0';
  return out;
}

static int confit_compat_find_equals(const char *text, size_t begin,
                                     size_t end, size_t *out_index) {
  size_t index;
  int in_string;
  char quote;

  in_string = 0;
  quote = '\0';
  for (index = begin; index < end; ++index) {
    if (in_string) {
      if (text[index] == '\\' && index + 1U < end) {
        index += 1U;
        continue;
      }
      if (text[index] == quote) {
        in_string = 0;
      }
      continue;
    }
    if (text[index] == '"' || text[index] == '\'') {
      in_string = 1;
      quote = text[index];
      continue;
    }
    if (text[index] == '=') {
      *out_index = index;
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_compat_parse_scalar(
    const char *text, size_t begin, size_t end, ConfitValue *out_value) {
  char *string_value;
  uint64_t uint_value;
  int negative;
  size_t index;

  begin = confit_compat_trim_left(text, begin, end);
  end = confit_compat_trim_right(text, begin, end);
  if (begin >= end) {
    return CONFIT_ERR_SCHEMA;
  }

  if (end - begin == 4U && strncmp(text + begin, "true", 4U) == 0) {
    confit_value_set_bool(out_value, 1);
    return CONFIT_OK;
  }
  if (end - begin == 5U && strncmp(text + begin, "false", 5U) == 0) {
    confit_value_set_bool(out_value, 0);
    return CONFIT_OK;
  }
  if (text[begin] == '"' || text[begin] == '\'') {
    string_value = confit_compat_parse_quoted_string(text, begin, end);
    if (string_value == 0) {
      return CONFIT_ERR_SCHEMA;
    }
    if (confit_value_set_string(out_value, string_value) != CONFIT_OK) {
      free(string_value);
      return CONFIT_ERR_INTERNAL;
    }
    free(string_value);
    return CONFIT_OK;
  }

  negative = 0;
  index = begin;
  if (text[index] == '-') {
    negative = 1;
    index += 1U;
  }
  if (index >= end) {
    return CONFIT_ERR_SCHEMA;
  }

  uint_value = 0U;
  for (; index < end; ++index) {
    if (text[index] < '0' || text[index] > '9') {
      return CONFIT_ERR_SCHEMA;
    }
    uint_value = uint_value * 10U + (uint64_t)(text[index] - '0');
  }

  if (negative) {
    confit_value_set_int(out_value, -(int64_t)uint_value);
  } else {
    confit_value_set_uint(out_value, uint_value);
  }
  return CONFIT_OK;
}

static ConfitStatus confit_compat_parse_condition(
    const char *text, size_t begin, size_t end,
    ConfitCompatCondition *condition) {
  char *option_id;
  ConfitValue value;
  size_t cursor;
  size_t option_begin;
  size_t equals_begin;
  size_t option_key;
  size_t equals_key;
  size_t comma;
  ConfitStatus status;

  begin = confit_compat_trim_left(text, begin, end);
  end = confit_compat_trim_right(text, begin, end);
  if (begin + 2U >= end || text[begin] != '{' || text[end - 1U] != '}') {
    return CONFIT_ERR_SCHEMA;
  }

  cursor = begin + 1U;
  cursor = confit_compat_trim_left(text, cursor, end - 1U);
  option_key = cursor;
  while (cursor < end - 1U && text[cursor] != '=') {
    cursor += 1U;
  }
  if (cursor >= end - 1U ||
      confit_compat_trim_right(text, option_key, cursor) - option_key != 6U ||
      strncmp(text + option_key, "option", 6U) != 0) {
    return CONFIT_ERR_SCHEMA;
  }
  option_begin = cursor + 1U;
  comma = option_begin;
  while (comma < end - 1U && text[comma] != ',') {
    comma += 1U;
  }
  if (comma >= end - 1U) {
    return CONFIT_ERR_SCHEMA;
  }
  option_id = confit_compat_parse_quoted_string(text, option_begin, comma);
  if (option_id == 0) {
    return CONFIT_ERR_SCHEMA;
  }

  cursor = confit_compat_trim_left(text, comma + 1U, end - 1U);
  equals_key = cursor;
  while (cursor < end - 1U && text[cursor] != '=') {
    cursor += 1U;
  }
  if (cursor >= end - 1U ||
      confit_compat_trim_right(text, equals_key, cursor) - equals_key != 6U ||
      strncmp(text + equals_key, "equals", 6U) != 0) {
    free(option_id);
    return CONFIT_ERR_SCHEMA;
  }
  equals_begin = cursor + 1U;

  confit_value_init(&value);
  status = confit_compat_parse_scalar(text, equals_begin, end - 1U, &value);
  if (status != CONFIT_OK) {
    free(option_id);
    return status;
  }

  free(condition->option_id);
  condition->option_id = option_id;
  confit_value_clear(&condition->equals);
  status = confit_value_copy(&condition->equals, &value);
  confit_value_clear(&value);
  return status;
}

static size_t confit_compat_next_line(const char *text, size_t text_size,
                                      size_t *offset, size_t *line_begin) {
  size_t begin;
  size_t end;

  begin = *offset;
  end = begin;
  while (end < text_size && text[end] != '\n') {
    end += 1U;
  }
  *line_begin = begin;
  *offset = end < text_size ? end + 1U : end;
  return end - begin;
}

static ConfitStatus confit_compat_parse_file(ConfitCompatSuite *suite,
                                             const char *path,
                                             ConfitDiagnostic *diagnostic) {
  ConfitParserDocument *document;
  const char *text;
  size_t text_size;
  size_t offset;
  ConfitCompatTableKind table_kind;
  ConfitCompatAssertion *current_assertion;
  ConfitStatus status;

  document = 0;
  status = confit_parser_load_file(path, &document, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }

  text = confit_parser_document_source_text(document);
  text_size = confit_parser_document_source_size(document);
  offset = 0U;
  table_kind = CONFIT_COMPAT_TABLE_NONE;
  current_assertion = 0;

  while (offset < text_size) {
    size_t line_begin;
    size_t line_size;
    size_t begin;
    size_t end;
    size_t equals_index;
    char *key;

    line_size = confit_compat_next_line(text, text_size, &offset, &line_begin);
    end = confit_compat_strip_comment(text, line_begin, line_begin + line_size);
    begin = confit_compat_trim_left(text, line_begin, end);
    end = confit_compat_trim_right(text, begin, end);
    if (begin >= end) {
      continue;
    }

    if (text[begin] == '[') {
      if (end - begin == 8U && strncmp(text + begin, "[compat]", 8U) == 0) {
        table_kind = CONFIT_COMPAT_TABLE_COMPAT;
        current_assertion = 0;
        continue;
      }
      if (end - begin == 10U &&
          strncmp(text + begin, "[[assert]]", 10U) == 0) {
        current_assertion = confit_compat_add_assertion(suite);
        if (current_assertion == 0) {
          confit_parser_document_free(document);
          confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, path, 0, 0,
                                "failed to allocate compatibility assertion");
          return CONFIT_ERR_INTERNAL;
        }
        status = confit_compat_replace_string(&current_assertion->source, path);
        if (status != CONFIT_OK) {
          confit_parser_document_free(document);
          confit_diagnostic_set(diagnostic, status, path, 0, 0,
                                "failed to allocate compatibility source");
          return status;
        }
        table_kind = CONFIT_COMPAT_TABLE_ASSERT;
        continue;
      }
      confit_parser_document_free(document);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path, 0, 0,
                            "unknown compatibility table");
      return CONFIT_ERR_SCHEMA;
    }

    if (!confit_compat_find_equals(text, begin, end, &equals_index)) {
      confit_parser_document_free(document);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path, 0, 0,
                            "expected compatibility key/value");
      return CONFIT_ERR_SCHEMA;
    }

    key = confit_compat_parse_bare_key(text, begin, equals_index);
    if (key == 0) {
      confit_parser_document_free(document);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path, 0, 0,
                            "invalid compatibility key");
      return CONFIT_ERR_SCHEMA;
    }

    if (table_kind == CONFIT_COMPAT_TABLE_COMPAT && strcmp(key, "name") == 0) {
      char *name =
          confit_compat_parse_quoted_string(text, equals_index + 1U, end);
      if (name == 0 ||
          confit_compat_replace_string(&suite->name, name) != CONFIT_OK) {
        free(name);
        free(key);
        confit_parser_document_free(document);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path, 0, 0,
                              "invalid compatibility name");
        return CONFIT_ERR_SCHEMA;
      }
      free(name);
    } else if (table_kind == CONFIT_COMPAT_TABLE_ASSERT &&
               current_assertion != 0 && strcmp(key, "when") == 0) {
      status = confit_compat_parse_condition(
          text, equals_index + 1U, end, &current_assertion->when);
      if (status != CONFIT_OK) {
        free(key);
        confit_parser_document_free(document);
        confit_diagnostic_set(diagnostic, status, path, 0, 0,
                              "invalid compatibility when");
        return status;
      }
    } else if (table_kind == CONFIT_COMPAT_TABLE_ASSERT &&
               current_assertion != 0 && strcmp(key, "requires") == 0) {
      current_assertion->action = CONFIT_COMPAT_REQUIRES;
      status = confit_compat_parse_condition(
          text, equals_index + 1U, end, &current_assertion->condition);
      if (status != CONFIT_OK) {
        free(key);
        confit_parser_document_free(document);
        confit_diagnostic_set(diagnostic, status, path, 0, 0,
                              "invalid compatibility requires");
        return status;
      }
    } else if (table_kind == CONFIT_COMPAT_TABLE_ASSERT &&
               current_assertion != 0 && strcmp(key, "forbids") == 0) {
      current_assertion->action = CONFIT_COMPAT_FORBIDS;
      status = confit_compat_parse_condition(
          text, equals_index + 1U, end, &current_assertion->condition);
      if (status != CONFIT_OK) {
        free(key);
        confit_parser_document_free(document);
        confit_diagnostic_set(diagnostic, status, path, 0, 0,
                              "invalid compatibility forbids");
        return status;
      }
    } else if (table_kind == CONFIT_COMPAT_TABLE_ASSERT &&
               current_assertion != 0 && strcmp(key, "message") == 0) {
      char *message =
          confit_compat_parse_quoted_string(text, equals_index + 1U, end);
      if (message == 0 ||
          confit_compat_replace_string(&current_assertion->message, message) !=
              CONFIT_OK) {
        free(message);
        free(key);
        confit_parser_document_free(document);
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path, 0, 0,
                              "invalid compatibility message");
        return CONFIT_ERR_SCHEMA;
      }
      free(message);
    } else {
      free(key);
      confit_parser_document_free(document);
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, path, 0, 0,
                            "unknown compatibility field");
      return CONFIT_ERR_SCHEMA;
    }

    free(key);
  }

  confit_parser_document_free(document);
  return CONFIT_OK;
}

static ConfitStatus confit_compat_validate_suite(
    const ConfitCompatSuite *suite, ConfitDiagnostic *diagnostic) {
  size_t index;

  for (index = 0U; index < suite->assertion_count; ++index) {
    const ConfitCompatAssertion *assertion = &suite->assertions[index];

    if (assertion->when.option_id == 0 || assertion->condition.option_id == 0 ||
        assertion->message == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, assertion->source, 0,
                            0, "incomplete compatibility assertion");
      return CONFIT_ERR_SCHEMA;
    }
  }
  return CONFIT_OK;
}

ConfitStatus confit_compat_load_directory(const char *compat_dir,
                                          ConfitCompatSuite **out_suite,
                                          ConfitDiagnostic *diagnostic) {
  ConfitCompatSuite *suite;
  char **paths;
  size_t path_count;
  size_t index;
  ConfitStatus status;

  if (out_suite == 0 || compat_dir == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, compat_dir, 0,
                          0, "invalid compatibility load argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_suite = 0;

  suite = confit_compat_suite_create();
  if (suite == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, compat_dir, 0, 0,
                          "failed to allocate compatibility suite");
    return CONFIT_ERR_INTERNAL;
  }

  paths = 0;
  path_count = 0U;
  status = confit_host_list_toml_files(compat_dir, &paths, &path_count,
                                       diagnostic);
  if (status != CONFIT_OK) {
    confit_compat_suite_free(suite);
    return status;
  }

  for (index = 0U; index < path_count; ++index) {
    status = confit_compat_parse_file(suite, paths[index], diagnostic);
    if (status != CONFIT_OK) {
      confit_host_string_list_free(paths, path_count);
      confit_compat_suite_free(suite);
      return status;
    }
  }

  confit_host_string_list_free(paths, path_count);
  status = confit_compat_validate_suite(suite, diagnostic);
  if (status != CONFIT_OK) {
    confit_compat_suite_free(suite);
    return status;
  }

  *out_suite = suite;
  return CONFIT_OK;
}

static const ConfitResolvedValue *confit_compat_find_resolved_value(
    const ConfitCompatProject *projects, size_t project_count,
    const char *option_id) {
  size_t index;

  for (index = 0U; index < project_count; ++index) {
    const ConfitResolvedValue *value;

    if (projects[index].config == 0) {
      continue;
    }
    value = confit_resolved_config_find(projects[index].config, option_id);
    if (value != 0) {
      return value;
    }
  }
  return 0;
}

static int confit_compat_values_equal(const ConfitValue *left,
                                      const ConfitValue *right) {
  if (left == 0 || right == 0) {
    return 0;
  }

  if ((left->kind == CONFIT_VALUE_STRING ||
       left->kind == CONFIT_VALUE_ENUM || left->kind == CONFIT_VALUE_PATH) &&
      (right->kind == CONFIT_VALUE_STRING ||
       right->kind == CONFIT_VALUE_ENUM || right->kind == CONFIT_VALUE_PATH)) {
    return left->as.string_value != 0 && right->as.string_value != 0 &&
           strcmp(left->as.string_value, right->as.string_value) == 0;
  }

  if (left->kind != right->kind) {
    return 0;
  }

  switch (left->kind) {
  case CONFIT_VALUE_BOOL:
    return left->as.bool_value == right->as.bool_value;
  case CONFIT_VALUE_INT:
    return left->as.int_value == right->as.int_value;
  case CONFIT_VALUE_UINT:
    return left->as.uint_value == right->as.uint_value;
  case CONFIT_VALUE_EMPTY:
    return 1;
  case CONFIT_VALUE_FLOAT:
  default:
    return 0;
  }
}

static ConfitStatus confit_compat_condition_matches(
    const ConfitCompatProject *projects, size_t project_count,
    const ConfitCompatCondition *condition, int *out_matches,
    ConfitDiagnostic *diagnostic) {
  const ConfitResolvedValue *resolved_value;

  resolved_value = confit_compat_find_resolved_value(projects, project_count,
                                                     condition->option_id);
  if (resolved_value == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_COMPATIBILITY,
                          condition->option_id, 0, 0,
                          "unknown compatibility option");
    return CONFIT_ERR_COMPATIBILITY;
  }

  *out_matches =
      confit_compat_values_equal(&resolved_value->value, &condition->equals);
  return CONFIT_OK;
}

ConfitStatus confit_compat_check(const ConfitCompatSuite *suite,
                                 const ConfitCompatProject *projects,
                                 size_t project_count,
                                 ConfitDiagnostic *diagnostic) {
  size_t index;

  if (suite == 0 || (project_count > 0U && projects == 0)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid compatibility check argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  for (index = 0U; index < suite->assertion_count; ++index) {
    const ConfitCompatAssertion *assertion = &suite->assertions[index];
    int when_matches;
    int condition_matches;
    ConfitStatus status;

    when_matches = 0;
    status = confit_compat_condition_matches(
        projects, project_count, &assertion->when, &when_matches, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    if (!when_matches) {
      continue;
    }

    condition_matches = 0;
    status = confit_compat_condition_matches(projects, project_count,
                                             &assertion->condition,
                                             &condition_matches, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }

    if ((assertion->action == CONFIT_COMPAT_REQUIRES && !condition_matches) ||
        (assertion->action == CONFIT_COMPAT_FORBIDS && condition_matches)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_COMPATIBILITY,
                            assertion->source, 0, 0, assertion->message);
      return CONFIT_ERR_COMPATIBILITY;
    }
  }

  return CONFIT_OK;
}
