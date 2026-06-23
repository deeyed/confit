#include "toml_scan.h"

#include <stdlib.h>

struct ConfitTomlScanDocument {
  size_t line_count;
  size_t table_count;
  size_t key_count;
};

typedef struct ConfitTomlValueState {
  size_t square_depth;
  size_t brace_depth;
  size_t open_line;
  size_t open_column;
} ConfitTomlValueState;

static void confit_toml_set_error(ConfitTomlScanError *error, size_t line,
                                  size_t column, const char *message) {
  if (error == 0) {
    return;
  }

  error->line = line;
  error->column = column;
  error->message = message;
}

static int confit_toml_is_space(char value) {
  return value == ' ' || value == '\t';
}

static int confit_toml_is_allowed_escape(char value) {
  return value == 'b' || value == 't' || value == 'n' || value == 'f' ||
         value == 'r' || value == '"' || value == '\\' || value == 'u' ||
         value == 'U';
}

static size_t confit_toml_trim_left(const char *line, size_t length) {
  size_t index;

  index = 0U;
  while (index < length && confit_toml_is_space(line[index])) {
    index += 1U;
  }

  return index;
}

static size_t confit_toml_trim_right(const char *line, size_t begin,
                                     size_t end) {
  while (end > begin && confit_toml_is_space(line[end - 1U])) {
    end -= 1U;
  }

  return end;
}

static int confit_toml_line_is_done(const char *line, size_t index,
                                    size_t length) {
  while (index < length && confit_toml_is_space(line[index])) {
    index += 1U;
  }

  return index >= length || line[index] == '#';
}

static int confit_toml_validate_key(const char *line, size_t begin, size_t end,
                                    size_t line_number,
                                    ConfitTomlScanError *error) {
  size_t index;
  int in_string;
  char quote;

  begin = confit_toml_trim_left(line + begin, end - begin) + begin;
  end = confit_toml_trim_right(line, begin, end);
  if (begin >= end) {
    confit_toml_set_error(error, line_number, begin + 1U, "missing key");
    return 0;
  }

  in_string = 0;
  quote = '\0';
  for (index = begin; index < end; ++index) {
    const char value = line[index];

    if (in_string != 0) {
      if (value == quote) {
        in_string = 0;
        quote = '\0';
      } else if (quote == '"' && value == '\\') {
        index += 1U;
        if (index >= end ||
            !confit_toml_is_allowed_escape(line[index])) {
          confit_toml_set_error(error, line_number, index + 1U,
                                "invalid key escape sequence");
          return 0;
        }
      }
      continue;
    }

    if (value == '"' || value == '\'') {
      in_string = 1;
      quote = value;
      continue;
    }

    if (value == '[' || value == ']' || value == '{' || value == '}') {
      confit_toml_set_error(error, line_number, index + 1U,
                            "invalid character in key");
      return 0;
    }
  }

  if (in_string != 0) {
    confit_toml_set_error(error, line_number, end, "unterminated quoted key");
    return 0;
  }

  return 1;
}

static int confit_toml_parse_table(const char *line, size_t length,
                                   size_t line_number,
                                   ConfitTomlScanDocument *document,
                                   ConfitTomlScanError *error) {
  const int array_table = length > 1U && line[1] == '[';
  size_t index;
  size_t body_begin;
  size_t body_end;
  int in_string;
  char quote;

  index = array_table ? 2U : 1U;
  body_begin = index;
  body_end = index;
  in_string = 0;
  quote = '\0';

  while (index < length) {
    const char value = line[index];

    if (in_string != 0) {
      if (value == quote) {
        in_string = 0;
        quote = '\0';
      } else if (quote == '"' && value == '\\') {
        index += 1U;
        if (index >= length ||
            !confit_toml_is_allowed_escape(line[index])) {
          confit_toml_set_error(error, line_number, index + 1U,
                                "invalid table escape sequence");
          return 0;
        }
      }
      index += 1U;
      continue;
    }

    if (value == '"' || value == '\'') {
      in_string = 1;
      quote = value;
      index += 1U;
      continue;
    }

    if (array_table != 0) {
      if (value == ']' && index + 1U < length && line[index + 1U] == ']') {
        body_end = index;
        index += 2U;
        if (!confit_toml_line_is_done(line, index, length)) {
          confit_toml_set_error(error, line_number, index + 1U,
                                "unexpected token after table header");
          return 0;
        }
        if (!confit_toml_validate_key(line, body_begin, body_end, line_number,
                                      error)) {
          return 0;
        }
        document->table_count += 1U;
        return 1;
      }
    } else if (value == ']') {
      body_end = index;
      index += 1U;
      if (!confit_toml_line_is_done(line, index, length)) {
        confit_toml_set_error(error, line_number, index + 1U,
                              "unexpected token after table header");
        return 0;
      }
      if (!confit_toml_validate_key(line, body_begin, body_end, line_number,
                                    error)) {
        return 0;
      }
      document->table_count += 1U;
      return 1;
    }

    index += 1U;
  }

  confit_toml_set_error(error, line_number, length + 1U,
                        "unterminated table header");
  return 0;
}

static int confit_toml_parse_value_fragment(const char *line, size_t begin,
                                            size_t length, size_t line_number,
                                            int require_value,
                                            ConfitTomlValueState *state,
                                            ConfitTomlScanError *error) {
  size_t index;
  int in_string;
  char quote;
  int saw_value;

  index = begin;
  in_string = 0;
  quote = '\0';
  saw_value = 0;

  while (index < length) {
    const char value = line[index];

    if (in_string != 0) {
      saw_value = 1;
      if (value == quote) {
        in_string = 0;
        quote = '\0';
      } else if (quote == '"' && value == '\\') {
        index += 1U;
        if (index >= length ||
            !confit_toml_is_allowed_escape(line[index])) {
          confit_toml_set_error(error, line_number, index + 1U,
                                "invalid string escape sequence");
          return 0;
        }
      }
      index += 1U;
      continue;
    }

    if (value == '#') {
      break;
    }

    if (confit_toml_is_space(value)) {
      index += 1U;
      continue;
    }

    saw_value = 1;
    if (value == '"' || value == '\'') {
      in_string = 1;
      quote = value;
    } else if (value == '[') {
      state->square_depth += 1U;
      state->open_line = line_number;
      state->open_column = index + 1U;
    } else if (value == ']') {
      if (state->square_depth == 0U) {
        confit_toml_set_error(error, line_number, index + 1U,
                              "unmatched closing array bracket");
        return 0;
      }
      state->square_depth -= 1U;
    } else if (value == '{') {
      state->brace_depth += 1U;
      state->open_line = line_number;
      state->open_column = index + 1U;
    } else if (value == '}') {
      if (state->brace_depth == 0U) {
        confit_toml_set_error(error, line_number, index + 1U,
                              "unmatched closing inline table brace");
        return 0;
      }
      state->brace_depth -= 1U;
    }

    index += 1U;
  }

  if (in_string != 0) {
    confit_toml_set_error(error, line_number, length + 1U,
                          "unterminated string");
    return 0;
  }

  if (require_value != 0 && saw_value == 0) {
    confit_toml_set_error(error, line_number, begin + 1U, "missing value");
    return 0;
  }

  return 1;
}

static int confit_toml_find_equals(const char *line, size_t length,
                                   size_t *out_index) {
  size_t index;
  int in_string;
  char quote;

  in_string = 0;
  quote = '\0';
  for (index = 0U; index < length; ++index) {
    const char value = line[index];

    if (in_string != 0) {
      if (value == quote) {
        in_string = 0;
        quote = '\0';
      } else if (quote == '"' && value == '\\') {
        index += 1U;
      }
      continue;
    }

    if (value == '"' || value == '\'') {
      in_string = 1;
      quote = value;
      continue;
    }

    if (value == '#') {
      return 0;
    }

    if (value == '=') {
      *out_index = index;
      return 1;
    }
  }

  return 0;
}

static int confit_toml_parse_key_value(const char *line, size_t begin,
                                       size_t length, size_t line_number,
                                       ConfitTomlScanDocument *document,
                                       ConfitTomlValueState *state,
                                       ConfitTomlScanError *error) {
  size_t equals_index;

  if (!confit_toml_find_equals(line + begin, length - begin, &equals_index)) {
    confit_toml_set_error(error, line_number, begin + 1U,
                          "expected '=' after key");
    return 0;
  }
  equals_index += begin;

  if (!confit_toml_validate_key(line, begin, equals_index, line_number,
                                error)) {
    return 0;
  }

  if (!confit_toml_parse_value_fragment(line, equals_index + 1U, length,
                                        line_number, 1, state, error)) {
    return 0;
  }

  document->key_count += 1U;
  return 1;
}

static int confit_toml_parse_line(const char *line, size_t length,
                                  size_t line_number,
                                  ConfitTomlScanDocument *document,
                                  ConfitTomlValueState *state,
                                  ConfitTomlScanError *error) {
  const size_t begin = confit_toml_trim_left(line, length);

  if (state->square_depth > 0U || state->brace_depth > 0U) {
    return confit_toml_parse_value_fragment(line, begin, length, line_number,
                                            0, state, error);
  }

  if (begin >= length || line[begin] == '#') {
    return 1;
  }

  if (line[begin] == '[') {
    return confit_toml_parse_table(line + begin, length - begin, line_number,
                                   document, error);
  }

  return confit_toml_parse_key_value(line, begin, length, line_number,
                                     document, state, error);
}

int confit_toml_scan_parse(const char *source, size_t source_size,
                             ConfitTomlScanDocument **out_document,
                             ConfitTomlScanError *error) {
  ConfitTomlScanDocument *document;
  ConfitTomlValueState state;
  size_t offset;
  size_t line_number;

  if (out_document == 0 || (source == 0 && source_size > 0U)) {
    confit_toml_set_error(error, 0, 0, "invalid parser argument");
    return 0;
  }

  *out_document = 0;
  document = (ConfitTomlScanDocument *)calloc(1U, sizeof(*document));
  if (document == 0) {
    confit_toml_set_error(error, 0, 0, "failed to allocate TOML document");
    return 0;
  }

  state.square_depth = 0U;
  state.brace_depth = 0U;
  state.open_line = 0U;
  state.open_column = 0U;
  offset = 0U;
  line_number = 1U;

  while (offset < source_size) {
    size_t line_begin;
    size_t line_length;

    line_begin = offset;
    while (offset < source_size && source[offset] != '\n') {
      offset += 1U;
    }

    line_length = offset - line_begin;
    if (line_length > 0U && source[line_begin + line_length - 1U] == '\r') {
      line_length -= 1U;
    }

    document->line_count += 1U;
    if (!confit_toml_parse_line(source + line_begin, line_length, line_number,
                                document, &state, error)) {
      free(document);
      return 0;
    }

    if (offset < source_size && source[offset] == '\n') {
      offset += 1U;
    }
    line_number += 1U;
  }

  if (state.square_depth > 0U || state.brace_depth > 0U) {
    confit_toml_set_error(error, state.open_line, state.open_column,
                          "unterminated value");
    free(document);
    return 0;
  }

  *out_document = document;
  return 1;
}

void confit_toml_scan_free(ConfitTomlScanDocument *document) {
  free(document);
}

size_t confit_toml_scan_line_count(const ConfitTomlScanDocument *document) {
  return document != 0 ? document->line_count : 0U;
}

size_t confit_toml_scan_table_count(const ConfitTomlScanDocument *document) {
  return document != 0 ? document->table_count : 0U;
}

size_t confit_toml_scan_key_count(const ConfitTomlScanDocument *document) {
  return document != 0 ? document->key_count : 0U;
}
