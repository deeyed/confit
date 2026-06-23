#include "confit/compat.h"

#include <stdio.h>
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

static const char *confit_compat_action_name(ConfitCompatActionKind action) {
  switch (action) {
  case CONFIT_COMPAT_REQUIRES:
    return "requires";
  case CONFIT_COMPAT_FORBIDS:
    return "forbids";
  default:
    return "unknown";
  }
}

static const char *confit_compat_result_status_name(
    ConfitCompatResultStatus status) {
  switch (status) {
  case CONFIT_COMPAT_RESULT_SKIPPED:
    return "skipped";
  case CONFIT_COMPAT_RESULT_PASS:
    return "pass";
  case CONFIT_COMPAT_RESULT_FAIL:
    return "fail";
  default:
    return "unknown";
  }
}

static const char *confit_compat_source_label(const char *path) {
  size_t index;
  size_t begin;

  if (path == 0) {
    return "";
  }

  begin = 0U;
  for (index = 0U; path[index] != '\0'; ++index) {
    if (path[index] == '/' || path[index] == '\\') {
      begin = index + 1U;
    }
  }
  return path + begin;
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

static void confit_compat_result_init(ConfitCompatResult *result) {
  result->assertion_index = 0U;
  result->status = CONFIT_COMPAT_RESULT_SKIPPED;
  result->action = CONFIT_COMPAT_REQUIRES;
  confit_compat_condition_init(&result->when);
  confit_compat_condition_init(&result->condition);
  result->message = 0;
  result->source = 0;
  result->when_project = 0;
  result->condition_project = 0;
  confit_value_init(&result->when_actual);
  confit_value_init(&result->condition_actual);
  result->has_when_actual = 0;
  result->has_condition_actual = 0;
  result->when_matches = 0;
  result->condition_matches = 0;
}

static void confit_compat_result_clear(ConfitCompatResult *result) {
  if (result == 0) {
    return;
  }

  confit_compat_condition_clear(&result->when);
  confit_compat_condition_clear(&result->condition);
  free(result->message);
  free(result->source);
  free(result->when_project);
  free(result->condition_project);
  confit_value_clear(&result->when_actual);
  confit_value_clear(&result->condition_actual);
  confit_compat_result_init(result);
}

static ConfitCompatSuite *confit_compat_suite_create(void) {
  return (ConfitCompatSuite *)calloc(1U, sizeof(ConfitCompatSuite));
}

static ConfitCompatReport *confit_compat_report_create(size_t result_count) {
  ConfitCompatReport *report;
  size_t index;

  report = (ConfitCompatReport *)calloc(1U, sizeof(ConfitCompatReport));
  if (report == 0) {
    return 0;
  }
  if (result_count > 0U) {
    report->results =
        (ConfitCompatResult *)calloc(result_count, sizeof(report->results[0]));
    if (report->results == 0) {
      free(report);
      return 0;
    }
    report->result_count = result_count;
    for (index = 0U; index < result_count; ++index) {
      confit_compat_result_init(&report->results[index]);
    }
  }
  return report;
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

void confit_compat_report_free(ConfitCompatReport *report) {
  size_t index;

  if (report == 0) {
    return;
  }

  for (index = 0U; index < report->result_count; ++index) {
    confit_compat_result_clear(&report->results[index]);
  }
  free(report->results);
  free(report->suite_name);
  free(report);
}

void confit_compat_string_free(char *text) { free(text); }

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

static ConfitStatus confit_compat_condition_copy(
    ConfitCompatCondition *out, const ConfitCompatCondition *input) {
  ConfitStatus status;

  if (out == 0 || input == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  status = confit_compat_replace_string(&out->option_id, input->option_id);
  if (status != CONFIT_OK) {
    return status;
  }
  confit_value_clear(&out->equals);
  return confit_value_copy(&out->equals, &input->equals);
}

static ConfitStatus confit_compat_result_from_assertion(
    ConfitCompatResult *result, const ConfitCompatAssertion *assertion,
    size_t assertion_index) {
  ConfitStatus status;

  result->assertion_index = assertion_index;
  result->status = CONFIT_COMPAT_RESULT_SKIPPED;
  result->action = assertion->action;

  status = confit_compat_condition_copy(&result->when, &assertion->when);
  if (status != CONFIT_OK) {
    return status;
  }
  status =
      confit_compat_condition_copy(&result->condition, &assertion->condition);
  if (status != CONFIT_OK) {
    return status;
  }
  status = confit_compat_replace_string(&result->message, assertion->message);
  if (status != CONFIT_OK) {
    return status;
  }
  return confit_compat_replace_string(&result->source, assertion->source);
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
    const char *option_id, const ConfitProject **out_project) {
  size_t index;

  if (out_project != 0) {
    *out_project = 0;
  }

  for (index = 0U; index < project_count; ++index) {
    const ConfitResolvedValue *value;

    if (projects[index].config == 0) {
      continue;
    }
    value = confit_resolved_config_find(projects[index].config, option_id);
    if (value != 0) {
      if (out_project != 0) {
        *out_project = projects[index].project;
      }
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

static ConfitStatus confit_compat_evaluate_condition(
    const ConfitCompatProject *projects, size_t project_count,
    const ConfitCompatCondition *condition, ConfitValue *actual,
    int *out_has_actual, char **out_project_name, int *out_matches) {
  const ConfitResolvedValue *resolved_value;
  const ConfitProject *project;
  ConfitStatus status;

  project = 0;
  resolved_value = confit_compat_find_resolved_value(
      projects, project_count, condition->option_id, &project);
  if (resolved_value == 0) {
    *out_has_actual = 0;
    *out_matches = 0;
    return CONFIT_ERR_COMPATIBILITY;
  }

  confit_value_clear(actual);
  status = confit_value_copy(actual, &resolved_value->value);
  if (status != CONFIT_OK) {
    return status;
  }
  *out_has_actual = 1;
  *out_matches = confit_compat_values_equal(actual, &condition->equals);

  if (out_project_name != 0) {
    status = confit_compat_replace_string(
        out_project_name, project != 0 && project->name != 0 ? project->name
                                                             : "");
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

ConfitStatus confit_compat_check_report(const ConfitCompatSuite *suite,
                                        const ConfitCompatProject *projects,
                                        size_t project_count,
                                        ConfitCompatReport **out_report,
                                        ConfitDiagnostic *diagnostic) {
  ConfitCompatReport *report;
  size_t index;
  int diagnostic_set;
  ConfitStatus final_status;

  if (suite == 0 || out_report == 0 || (project_count > 0U && projects == 0)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid compatibility report argument");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_report = 0;

  report = confit_compat_report_create(suite->assertion_count);
  if (report == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          "failed to allocate compatibility report");
    return CONFIT_ERR_INTERNAL;
  }
  if (suite->name != 0 &&
      confit_compat_replace_string(&report->suite_name, suite->name) !=
          CONFIT_OK) {
    confit_compat_report_free(report);
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          "failed to allocate compatibility suite name");
    return CONFIT_ERR_INTERNAL;
  }

  diagnostic_set = 0;
  final_status = CONFIT_OK;
  for (index = 0U; index < suite->assertion_count; ++index) {
    const ConfitCompatAssertion *assertion = &suite->assertions[index];
    ConfitCompatResult *result;
    ConfitStatus status;

    result = &report->results[index];
    status = confit_compat_result_from_assertion(result, assertion, index);
    if (status != CONFIT_OK) {
      confit_compat_report_free(report);
      confit_diagnostic_set(diagnostic, status, assertion->source, 0, 0,
                            "failed to allocate compatibility result");
      return status;
    }

    status = confit_compat_evaluate_condition(
        projects, project_count, &assertion->when, &result->when_actual,
        &result->has_when_actual, &result->when_project,
        &result->when_matches);
    if (status == CONFIT_ERR_COMPATIBILITY) {
      (void)confit_compat_replace_string(&result->message,
                                         "unknown compatibility option");
      result->status = CONFIT_COMPAT_RESULT_FAIL;
      report->fail_count += 1U;
      final_status = CONFIT_ERR_COMPATIBILITY;
      if (!diagnostic_set) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_COMPATIBILITY,
                              assertion->when.option_id, 0, 0,
                              "unknown compatibility option");
        diagnostic_set = 1;
      }
      continue;
    }
    if (status != CONFIT_OK) {
      confit_compat_report_free(report);
      confit_diagnostic_set(diagnostic, status, assertion->source, 0, 0,
                            "failed to evaluate compatibility condition");
      return status;
    }

    if (!result->when_matches) {
      result->status = CONFIT_COMPAT_RESULT_SKIPPED;
      report->skipped_count += 1U;
      continue;
    }

    status = confit_compat_evaluate_condition(
        projects, project_count, &assertion->condition,
        &result->condition_actual, &result->has_condition_actual,
        &result->condition_project, &result->condition_matches);
    if (status == CONFIT_ERR_COMPATIBILITY) {
      (void)confit_compat_replace_string(&result->message,
                                         "unknown compatibility option");
      result->status = CONFIT_COMPAT_RESULT_FAIL;
      report->fail_count += 1U;
      final_status = CONFIT_ERR_COMPATIBILITY;
      if (!diagnostic_set) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_COMPATIBILITY,
                              assertion->condition.option_id, 0, 0,
                              "unknown compatibility option");
        diagnostic_set = 1;
      }
      continue;
    }
    if (status != CONFIT_OK) {
      confit_compat_report_free(report);
      confit_diagnostic_set(diagnostic, status, assertion->source, 0, 0,
                            "failed to evaluate compatibility condition");
      return status;
    }

    if ((assertion->action == CONFIT_COMPAT_REQUIRES &&
         !result->condition_matches) ||
        (assertion->action == CONFIT_COMPAT_FORBIDS &&
         result->condition_matches)) {
      result->status = CONFIT_COMPAT_RESULT_FAIL;
      report->fail_count += 1U;
      final_status = CONFIT_ERR_COMPATIBILITY;
      if (!diagnostic_set) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_COMPATIBILITY,
                              assertion->source, 0, 0, assertion->message);
        diagnostic_set = 1;
      }
    } else {
      result->status = CONFIT_COMPAT_RESULT_PASS;
      report->pass_count += 1U;
    }
  }

  *out_report = report;
  return final_status;
}

ConfitStatus confit_compat_check(const ConfitCompatSuite *suite,
                                 const ConfitCompatProject *projects,
                                 size_t project_count,
                                 ConfitDiagnostic *diagnostic) {
  ConfitCompatReport *report;
  ConfitStatus status;

  report = 0;
  status = confit_compat_check_report(suite, projects, project_count, &report,
                                      diagnostic);
  confit_compat_report_free(report);
  return status;
}

typedef struct ConfitCompatJsonBuilder {
  char *text;
  size_t size;
  size_t capacity;
} ConfitCompatJsonBuilder;

static void confit_compat_json_builder_init(ConfitCompatJsonBuilder *builder) {
  builder->text = 0;
  builder->size = 0U;
  builder->capacity = 0U;
}

static ConfitStatus confit_compat_json_reserve(ConfitCompatJsonBuilder *builder,
                                               size_t extra) {
  char *new_text;
  size_t needed;
  size_t new_capacity;

  needed = builder->size + extra + 1U;
  if (needed <= builder->capacity) {
    return CONFIT_OK;
  }

  new_capacity = builder->capacity == 0U ? 256U : builder->capacity;
  while (new_capacity < needed) {
    new_capacity *= 2U;
  }

  new_text = (char *)realloc(builder->text, new_capacity);
  if (new_text == 0) {
    return CONFIT_ERR_INTERNAL;
  }

  builder->text = new_text;
  builder->capacity = new_capacity;
  return CONFIT_OK;
}

static ConfitStatus confit_compat_json_append(ConfitCompatJsonBuilder *builder,
                                              const char *text) {
  size_t size;
  ConfitStatus status;

  if (text == 0) {
    text = "";
  }
  size = strlen(text);
  status = confit_compat_json_reserve(builder, size);
  if (status != CONFIT_OK) {
    return status;
  }

  memcpy(builder->text + builder->size, text, size);
  builder->size += size;
  builder->text[builder->size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_compat_json_append_escaped(
    ConfitCompatJsonBuilder *builder, const char *text) {
  ConfitStatus status;

  status = confit_compat_json_append(builder, "\"");
  if (status != CONFIT_OK) {
    return status;
  }

  if (text != 0) {
    while (*text != '\0') {
      char ch[3];

      if (*text == '"' || *text == '\\') {
        ch[0] = '\\';
        ch[1] = *text;
        ch[2] = '\0';
        status = confit_compat_json_append(builder, ch);
      } else if (*text == '\n') {
        status = confit_compat_json_append(builder, "\\n");
      } else if (*text == '\r') {
        status = confit_compat_json_append(builder, "\\r");
      } else if (*text == '\t') {
        status = confit_compat_json_append(builder, "\\t");
      } else {
        ch[0] = *text;
        ch[1] = '\0';
        status = confit_compat_json_append(builder, ch);
      }
      if (status != CONFIT_OK) {
        return status;
      }
      text += 1;
    }
  }

  return confit_compat_json_append(builder, "\"");
}

static ConfitStatus confit_compat_json_append_size(
    ConfitCompatJsonBuilder *builder, size_t value) {
  char buffer[64];

  (void)snprintf(buffer, sizeof(buffer), "%lu", (unsigned long)value);
  return confit_compat_json_append(builder, buffer);
}

static ConfitStatus confit_compat_json_append_int64(
    ConfitCompatJsonBuilder *builder, int64_t value) {
  char buffer[64];

  (void)snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
  return confit_compat_json_append(builder, buffer);
}

static ConfitStatus confit_compat_json_append_uint64(
    ConfitCompatJsonBuilder *builder, uint64_t value) {
  char buffer[64];

  (void)snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
  return confit_compat_json_append(builder, buffer);
}

static ConfitStatus confit_compat_json_append_float(
    ConfitCompatJsonBuilder *builder, double value) {
  char buffer[64];

  (void)snprintf(buffer, sizeof(buffer), "%.17g", value);
  return confit_compat_json_append(builder, buffer);
}

static ConfitStatus confit_compat_json_append_value(
    ConfitCompatJsonBuilder *builder, const ConfitValue *value) {
  if (value == 0) {
    return confit_compat_json_append(builder, "null");
  }

  switch (value->kind) {
  case CONFIT_VALUE_BOOL:
    return confit_compat_json_append(builder,
                                     value->as.bool_value ? "true" : "false");
  case CONFIT_VALUE_INT:
    return confit_compat_json_append_int64(builder, value->as.int_value);
  case CONFIT_VALUE_UINT:
    return confit_compat_json_append_uint64(builder, value->as.uint_value);
  case CONFIT_VALUE_FLOAT:
    return confit_compat_json_append_float(builder, value->as.float_value);
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    return confit_compat_json_append_escaped(builder, value->as.string_value);
  case CONFIT_VALUE_EMPTY:
  default:
    return confit_compat_json_append(builder, "null");
  }
}

static ConfitStatus confit_compat_json_append_condition(
    ConfitCompatJsonBuilder *builder, const ConfitCompatCondition *condition,
    const ConfitValue *actual, int has_actual, const char *project_name,
    int matches, int evaluated) {
  ConfitStatus status;

#define CONFIT_COMPAT_JSON_COND_APPEND(fragment)                                \
  do {                                                                          \
    status = confit_compat_json_append(builder, (fragment));                    \
    if (status != CONFIT_OK) {                                                  \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_COMPAT_JSON_COND_STRING(fragment)                                \
  do {                                                                          \
    status = confit_compat_json_append_escaped(builder, (fragment));            \
    if (status != CONFIT_OK) {                                                  \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_COMPAT_JSON_COND_VALUE(value)                                    \
  do {                                                                          \
    status = confit_compat_json_append_value(builder, (value));                 \
    if (status != CONFIT_OK) {                                                  \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_COMPAT_JSON_COND_APPEND("{\"option\": ");
  CONFIT_COMPAT_JSON_COND_STRING(condition->option_id);
  CONFIT_COMPAT_JSON_COND_APPEND(", \"expected\": ");
  CONFIT_COMPAT_JSON_COND_VALUE(&condition->equals);
  CONFIT_COMPAT_JSON_COND_APPEND(", \"actual\": ");
  if (has_actual) {
    CONFIT_COMPAT_JSON_COND_VALUE(actual);
  } else {
    CONFIT_COMPAT_JSON_COND_APPEND("null");
  }
  CONFIT_COMPAT_JSON_COND_APPEND(", \"project\": ");
  CONFIT_COMPAT_JSON_COND_STRING(project_name != 0 ? project_name : "");
  CONFIT_COMPAT_JSON_COND_APPEND(", \"matches\": ");
  CONFIT_COMPAT_JSON_COND_APPEND(matches ? "true" : "false");
  CONFIT_COMPAT_JSON_COND_APPEND(", \"evaluated\": ");
  CONFIT_COMPAT_JSON_COND_APPEND(evaluated ? "true" : "false");
  CONFIT_COMPAT_JSON_COND_APPEND("}");

#undef CONFIT_COMPAT_JSON_COND_APPEND
#undef CONFIT_COMPAT_JSON_COND_STRING
#undef CONFIT_COMPAT_JSON_COND_VALUE

  return CONFIT_OK;
}

ConfitStatus confit_compat_report_to_json(const ConfitCompatReport *report,
                                          char **out_json) {
  ConfitCompatJsonBuilder builder;
  ConfitStatus status;
  size_t index;

  if (report == 0 || out_json == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  *out_json = 0;
  confit_compat_json_builder_init(&builder);

#define CONFIT_COMPAT_JSON_APPEND(fragment)                                     \
  do {                                                                          \
    status = confit_compat_json_append(&builder, (fragment));                   \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_COMPAT_JSON_STRING(fragment)                                     \
  do {                                                                          \
    status = confit_compat_json_append_escaped(&builder, (fragment));           \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_COMPAT_JSON_SIZE(value)                                          \
  do {                                                                          \
    status = confit_compat_json_append_size(&builder, (value));                 \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

#define CONFIT_COMPAT_JSON_CONDITION(call_expr)                                 \
  do {                                                                          \
    status = (call_expr);                                                       \
    if (status != CONFIT_OK) {                                                  \
      free(builder.text);                                                       \
      return status;                                                            \
    }                                                                           \
  } while (0)

  CONFIT_COMPAT_JSON_APPEND("{\n");
  CONFIT_COMPAT_JSON_APPEND("  \"schema\": \"confit-compat-report-v1\",\n");
  CONFIT_COMPAT_JSON_APPEND("  \"suite\": ");
  CONFIT_COMPAT_JSON_STRING(report->suite_name != 0 ? report->suite_name : "");
  CONFIT_COMPAT_JSON_APPEND(",\n");
  CONFIT_COMPAT_JSON_APPEND("  \"status\": ");
  CONFIT_COMPAT_JSON_STRING(report->fail_count == 0U ? "ok" : "failed");
  CONFIT_COMPAT_JSON_APPEND(",\n");
  CONFIT_COMPAT_JSON_APPEND("  \"summary\": {\"assertions\": ");
  CONFIT_COMPAT_JSON_SIZE(report->result_count);
  CONFIT_COMPAT_JSON_APPEND(", \"passed\": ");
  CONFIT_COMPAT_JSON_SIZE(report->pass_count);
  CONFIT_COMPAT_JSON_APPEND(", \"failed\": ");
  CONFIT_COMPAT_JSON_SIZE(report->fail_count);
  CONFIT_COMPAT_JSON_APPEND(", \"skipped\": ");
  CONFIT_COMPAT_JSON_SIZE(report->skipped_count);
  CONFIT_COMPAT_JSON_APPEND("},\n");
  CONFIT_COMPAT_JSON_APPEND("  \"results\": [\n");
  for (index = 0U; index < report->result_count; ++index) {
    const ConfitCompatResult *result = &report->results[index];

    CONFIT_COMPAT_JSON_APPEND("    {\"index\": ");
    CONFIT_COMPAT_JSON_SIZE(result->assertion_index);
    CONFIT_COMPAT_JSON_APPEND(", \"status\": ");
    CONFIT_COMPAT_JSON_STRING(
        confit_compat_result_status_name(result->status));
    CONFIT_COMPAT_JSON_APPEND(", \"action\": ");
    CONFIT_COMPAT_JSON_STRING(confit_compat_action_name(result->action));
    CONFIT_COMPAT_JSON_APPEND(", \"source\": ");
    CONFIT_COMPAT_JSON_STRING(confit_compat_source_label(result->source));
    CONFIT_COMPAT_JSON_APPEND(", \"message\": ");
    CONFIT_COMPAT_JSON_STRING(result->message != 0 ? result->message : "");
    CONFIT_COMPAT_JSON_APPEND(", \"when\": ");
    CONFIT_COMPAT_JSON_CONDITION(confit_compat_json_append_condition(
        &builder, &result->when, &result->when_actual,
        result->has_when_actual, result->when_project, result->when_matches,
        1));
    CONFIT_COMPAT_JSON_APPEND(", \"condition\": ");
    CONFIT_COMPAT_JSON_CONDITION(confit_compat_json_append_condition(
        &builder, &result->condition, &result->condition_actual,
        result->has_condition_actual, result->condition_project,
        result->condition_matches, result->when_matches));
    CONFIT_COMPAT_JSON_APPEND("}");
    CONFIT_COMPAT_JSON_APPEND(index + 1U < report->result_count ? ",\n"
                                                                : "\n");
  }
  CONFIT_COMPAT_JSON_APPEND("  ]\n");
  CONFIT_COMPAT_JSON_APPEND("}\n");

#undef CONFIT_COMPAT_JSON_APPEND
#undef CONFIT_COMPAT_JSON_STRING
#undef CONFIT_COMPAT_JSON_SIZE
#undef CONFIT_COMPAT_JSON_CONDITION

  *out_json = builder.text;
  return CONFIT_OK;
}
