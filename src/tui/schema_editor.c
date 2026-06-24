#include "tui_internal.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/graph.h"
#include "confit/host.h"
#include "confit/schema.h"

typedef struct ConfitTuiSchemaOption {
  char id[128];
  char type[16];
  char default_value[128];
  char prompt[128];
  char help[256];
  char category[64];
  char tags[128];
  char range[128];
  char choices[128];
} ConfitTuiSchemaOption;

typedef struct ConfitTuiSchemaState {
  const ConfitTuiOptions *options;
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitTuiSchemaOption *options_list;
  size_t option_count;
  size_t selected_index;
  char status[256];
  int dirty;
} ConfitTuiSchemaState;

typedef enum ConfitTuiSchemaField {
  CONFIT_TUI_SCHEMA_FIELD_ID = 1,
  CONFIT_TUI_SCHEMA_FIELD_TYPE = 2,
  CONFIT_TUI_SCHEMA_FIELD_DEFAULT = 3,
  CONFIT_TUI_SCHEMA_FIELD_PROMPT = 4,
  CONFIT_TUI_SCHEMA_FIELD_HELP = 5,
  CONFIT_TUI_SCHEMA_FIELD_CATEGORY = 6,
  CONFIT_TUI_SCHEMA_FIELD_TAGS = 7,
  CONFIT_TUI_SCHEMA_FIELD_RANGE = 8,
  CONFIT_TUI_SCHEMA_FIELD_CHOICES = 9,
} ConfitTuiSchemaField;

typedef struct ConfitTuiSchemaFieldValidator {
  const ConfitTuiSchemaState *state;
  const ConfitTuiSchemaOption *option;
  ConfitTuiSchemaField field;
} ConfitTuiSchemaFieldValidator;

static ConfitStatus
confit_tui_schema_validate_range(const ConfitTuiSchemaOption *option,
                                 ConfitDiagnostic *diagnostic);

static void confit_tui_schema_validation_message(char *message,
                                                 size_t message_size,
                                                 const char *text) {
  size_t length;

  if (message == 0 || message_size == 0U) {
    return;
  }
  if (text == 0) {
    text = "";
  }
  length = strlen(text);
  if (length >= message_size) {
    length = message_size - 1U;
  }
  memcpy(message, text, length);
  message[length] = '\0';
}

static void confit_tui_schema_copy_text(char *out, size_t out_size,
                                        const char *text) {
  confit_tui_schema_validation_message(out, out_size, text);
}

static void confit_tui_schema_append_text(char *out, size_t out_size,
                                          const char *text) {
  size_t used;

  if (out == 0 || out_size == 0U || text == 0 || text[0] == '\0') {
    return;
  }
  used = strlen(out);
  if (used + 1U >= out_size) {
    return;
  }
  confit_tui_schema_copy_text(out + used, out_size - used, text);
}

static void confit_tui_schema_set_status_from_diagnostic(
    ConfitTuiSchemaState *state, const char *prefix, ConfitStatus status,
    const ConfitDiagnostic *diagnostic) {
  const char *message;

  if (state == 0) {
    return;
  }
  message = diagnostic != 0 && diagnostic->message != 0
                ? diagnostic->message
                : confit_status_name(status);
  if (diagnostic != 0 && diagnostic->path != 0 && diagnostic->path[0] != '\0') {
    (void)snprintf(state->status, sizeof(state->status), "%s: %s: %s",
                   confit_tui_text_or_dash(prefix), diagnostic->path, message);
  } else {
    (void)snprintf(state->status, sizeof(state->status), "%s: %s",
                   confit_tui_text_or_dash(prefix), message);
  }
  state->status[sizeof(state->status) - 1U] = '\0';
}

static int confit_tui_schema_valid_id_char(char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9') || value == '_' || value == '-';
}

static int confit_tui_schema_valid_option_id(const char *id) {
  size_t index;
  int saw_dot;
  int segment_has_char;

  if (id == 0 || id[0] == '\0' || id[0] == '.' || strcmp(id, "system") == 0 ||
      strncmp(id, "system.", 7U) == 0) {
    return 0;
  }

  saw_dot = 0;
  segment_has_char = 0;
  for (index = 0U; id[index] != '\0'; ++index) {
    if (id[index] == '.') {
      if (!segment_has_char) {
        return 0;
      }
      saw_dot = 1;
      segment_has_char = 0;
      continue;
    }
    if (!confit_tui_schema_valid_id_char(id[index])) {
      return 0;
    }
    segment_has_char = 1;
  }
  return saw_dot && segment_has_char;
}

static int confit_tui_schema_type_is_valid(const char *type) {
  return strcmp(type, "bool") == 0 || strcmp(type, "int") == 0 ||
         strcmp(type, "uint") == 0 || strcmp(type, "hex") == 0 ||
         strcmp(type, "string") == 0 || strcmp(type, "enum") == 0 ||
         strcmp(type, "float") == 0 || strcmp(type, "path") == 0;
}

static ConfitOptionType confit_tui_schema_type_from_string(const char *type) {
  if (strcmp(type, "bool") == 0) {
    return CONFIT_OPTION_TYPE_BOOL;
  }
  if (strcmp(type, "int") == 0) {
    return CONFIT_OPTION_TYPE_INT;
  }
  if (strcmp(type, "uint") == 0) {
    return CONFIT_OPTION_TYPE_UINT;
  }
  if (strcmp(type, "hex") == 0) {
    return CONFIT_OPTION_TYPE_HEX;
  }
  if (strcmp(type, "string") == 0) {
    return CONFIT_OPTION_TYPE_STRING;
  }
  if (strcmp(type, "enum") == 0) {
    return CONFIT_OPTION_TYPE_ENUM;
  }
  if (strcmp(type, "float") == 0) {
    return CONFIT_OPTION_TYPE_FLOAT;
  }
  if (strcmp(type, "path") == 0) {
    return CONFIT_OPTION_TYPE_PATH;
  }
  return CONFIT_OPTION_TYPE_INVALID;
}

static int confit_tui_schema_text_has_control(const char *text) {
  size_t index;

  for (index = 0U; text != 0 && text[index] != '\0'; ++index) {
    if (iscntrl((unsigned char)text[index])) {
      return 1;
    }
  }
  return 0;
}

static int confit_tui_schema_path_is_absolute(const char *text) {
  if (text == 0 || text[0] == '\0') {
    return 0;
  }
  return text[0] == '/' || text[0] == '\\';
}

static void confit_tui_schema_default_for_type(ConfitTuiSchemaOption *option) {
  if (strcmp(option->type, "bool") == 0) {
    (void)snprintf(option->default_value, sizeof(option->default_value),
                   "false");
  } else if (strcmp(option->type, "int") == 0 ||
             strcmp(option->type, "uint") == 0) {
    (void)snprintf(option->default_value, sizeof(option->default_value), "0");
  } else if (strcmp(option->type, "hex") == 0) {
    (void)snprintf(option->default_value, sizeof(option->default_value), "0x0");
  } else if (strcmp(option->type, "float") == 0) {
    (void)snprintf(option->default_value, sizeof(option->default_value), "0.0");
  } else {
    option->default_value[0] = '\0';
  }
}

static ConfitTuiSchemaOption *
confit_tui_schema_selected(ConfitTuiSchemaState *state) {
  if (state->option_count == 0U ||
      state->selected_index >= state->option_count) {
    return 0;
  }
  return &state->options_list[state->selected_index];
}

static int
confit_tui_schema_draft_id_exists(const ConfitTuiSchemaState *state,
                                  const char *id,
                                  const ConfitTuiSchemaOption *skip) {
  size_t index;

  if (state == 0 || id == 0) {
    return 0;
  }
  for (index = 0U; index < state->option_count; ++index) {
    if (&state->options_list[index] != skip &&
        strcmp(state->options_list[index].id, id) == 0) {
      return 1;
    }
  }
  return 0;
}

static int confit_tui_schema_csv_validate(const char *csv, int allow_empty,
                                          char *message, size_t message_size) {
  char scratch[256];
  char *cursor;
  char *token;
  char seen[32][64];
  size_t seen_count;

  if (csv == 0 || csv[0] == '\0') {
    if (allow_empty) {
      return 0;
    }
    confit_tui_schema_validation_message(
        message, message_size, "comma-list requires at least one item");
    return 1;
  }
  if (confit_tui_schema_text_has_control(csv)) {
    confit_tui_schema_validation_message(message, message_size,
                                         "comma-list contains control chars");
    return 1;
  }
  (void)snprintf(scratch, sizeof(scratch), "%s", csv);
  scratch[sizeof(scratch) - 1U] = '\0';
  cursor = scratch;
  seen_count = 0U;
  while ((token = strtok(cursor, ",")) != 0) {
    char *end;
    size_t index;

    cursor = 0;
    while (*token == ' ' || *token == '\t') {
      token += 1;
    }
    end = token + strlen(token);
    while (end > token && (end[-1] == ' ' || end[-1] == '\t')) {
      end -= 1;
    }
    *end = '\0';
    if (token[0] == '\0') {
      confit_tui_schema_validation_message(message, message_size,
                                           "comma-list has an empty item");
      return 1;
    }
    for (index = 0U; index < seen_count; ++index) {
      if (strcmp(seen[index], token) == 0) {
        confit_tui_schema_validation_message(message, message_size,
                                             "comma-list has duplicate item");
        return 1;
      }
    }
    if (seen_count < 32U) {
      (void)snprintf(seen[seen_count], sizeof(seen[seen_count]), "%s", token);
      seen[seen_count][sizeof(seen[seen_count]) - 1U] = '\0';
      seen_count += 1U;
    }
  }
  if (seen_count == 0U && !allow_empty) {
    confit_tui_schema_validation_message(
        message, message_size, "comma-list requires at least one item");
    return 1;
  }
  return 0;
}

static ConfitStatus confit_tui_schema_first_choice(const char *choices,
                                                   char *out, size_t out_size) {
  char *comma;

  if (out == 0 || out_size == 0U) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  out[0] = '\0';
  if (choices == 0 || choices[0] == '\0') {
    return CONFIT_ERR_SCHEMA;
  }
  (void)snprintf(out, out_size, "%s", choices);
  out[out_size - 1U] = '\0';
  comma = strchr(out, ',');
  if (comma != 0) {
    *comma = '\0';
  }
  while (out[0] == ' ' || out[0] == '\t') {
    memmove(out, out + 1, strlen(out));
  }
  while (out[0] != '\0') {
    const size_t size = strlen(out);
    if (out[size - 1U] != ' ' && out[size - 1U] != '\t') {
      break;
    }
    out[size - 1U] = '\0';
  }
  return out[0] != '\0' ? CONFIT_OK : CONFIT_ERR_SCHEMA;
}

static ConfitStatus
confit_tui_schema_parse_default_value(const ConfitTuiSchemaOption *option,
                                      const char *text, ConfitValue *out_value,
                                      char *message, size_t message_size) {
  const char *input = text != 0 ? text : "";

  confit_value_init(out_value);
  if (strcmp(option->type, "bool") == 0) {
    if (strcmp(input, "true") == 0 || strcmp(input, "1") == 0) {
      confit_value_set_bool(out_value, 1);
      return CONFIT_OK;
    }
    if (strcmp(input, "false") == 0 || strcmp(input, "0") == 0) {
      confit_value_set_bool(out_value, 0);
      return CONFIT_OK;
    }
    confit_tui_schema_validation_message(message, message_size,
                                         "default bool must be true or false");
    return CONFIT_ERR_SCHEMA;
  }
  if (strcmp(option->type, "int") == 0) {
    int64_t value;
    if (confit_tui_parse_int64(input, &value) == CONFIT_OK) {
      confit_value_set_int(out_value, value);
      return CONFIT_OK;
    }
    confit_tui_schema_validation_message(message, message_size,
                                         "default int must be an integer");
    return CONFIT_ERR_SCHEMA;
  }
  if (strcmp(option->type, "uint") == 0 || strcmp(option->type, "hex") == 0) {
    uint64_t value;
    if (confit_tui_parse_uint64(input, &value) == CONFIT_OK) {
      confit_value_set_uint(out_value, value);
      return CONFIT_OK;
    }
    confit_tui_schema_validation_message(message, message_size,
                                         "default uint must be unsigned");
    return CONFIT_ERR_SCHEMA;
  }
  if (strcmp(option->type, "float") == 0) {
    double value;
    if (confit_tui_parse_double(input, &value) == CONFIT_OK && value == value &&
        value <= 1.0e308 && value >= -1.0e308) {
      confit_value_set_float(out_value, value);
      return CONFIT_OK;
    }
    confit_tui_schema_validation_message(message, message_size,
                                         "default float must be finite");
    return CONFIT_ERR_SCHEMA;
  }
  if (strcmp(option->type, "enum") == 0) {
    char scratch[256];
    char *cursor;
    char *token;

    if (input[0] == '\0') {
      confit_tui_schema_validation_message(message, message_size,
                                           "default enum value required");
      return CONFIT_ERR_SCHEMA;
    }
    (void)snprintf(scratch, sizeof(scratch), "%s", option->choices);
    scratch[sizeof(scratch) - 1U] = '\0';
    cursor = scratch;
    while ((token = strtok(cursor, ",")) != 0) {
      char *end;

      cursor = 0;
      while (*token == ' ' || *token == '\t') {
        token += 1;
      }
      end = token + strlen(token);
      while (end > token && (end[-1] == ' ' || end[-1] == '\t')) {
        end -= 1;
      }
      *end = '\0';
      if (strcmp(token, input) == 0) {
        return confit_value_set_enum(out_value, input);
      }
    }
    confit_tui_schema_validation_message(message, message_size,
                                         "default enum is not a choice");
    return CONFIT_ERR_SCHEMA;
  }
  if (strcmp(option->type, "string") == 0) {
    if (confit_tui_schema_text_has_control(input)) {
      confit_tui_schema_validation_message(message, message_size,
                                           "default string has control chars");
      return CONFIT_ERR_SCHEMA;
    }
    return confit_value_set_string(out_value, input);
  }
  if (strcmp(option->type, "path") == 0) {
    if (confit_tui_schema_text_has_control(input)) {
      confit_tui_schema_validation_message(message, message_size,
                                           "default path has control chars");
      return CONFIT_ERR_SCHEMA;
    }
    if (confit_tui_schema_path_is_absolute(input)) {
      confit_tui_schema_validation_message(message, message_size,
                                           "default path must be relative");
      return CONFIT_ERR_SCHEMA;
    }
    return confit_value_set_path(out_value, input);
  }
  confit_tui_schema_validation_message(message, message_size,
                                       "invalid schema option type");
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus confit_tui_schema_load(ConfitTuiSchemaState *state,
                                           ConfitDiagnostic *diagnostic) {
  ConfitStatus status;

  status = confit_schema_load_project(state->options->project_root,
                                      &state->project, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_graph_build(state->project, &state->graph, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_graph_validate(state->graph, diagnostic);
  }
  return status;
}

static int confit_tui_schema_field_validator(const char *text, char *message,
                                             size_t message_size, void *user) {
  ConfitTuiSchemaFieldValidator *validator =
      (ConfitTuiSchemaFieldValidator *)user;

  if (validator == 0) {
    confit_tui_schema_validation_message(message, message_size,
                                         "invalid schema field validator");
    return 1;
  }
  switch (validator->field) {
  case CONFIT_TUI_SCHEMA_FIELD_ID:
    if (!confit_tui_schema_valid_option_id(text)) {
      confit_tui_schema_validation_message(message, message_size,
                                           "invalid schema option id");
      return 1;
    }
    if (confit_project_find_option((ConfitProject *)validator->state->project,
                                   text) != 0) {
      confit_tui_schema_validation_message(message, message_size,
                                           "schema option already exists");
      return 1;
    }
    if (confit_tui_schema_draft_id_exists(validator->state, text,
                                          validator->option)) {
      confit_tui_schema_validation_message(message, message_size,
                                           "schema draft already exists");
      return 1;
    }
    return 0;
  case CONFIT_TUI_SCHEMA_FIELD_TYPE:
    if (!confit_tui_schema_type_is_valid(text)) {
      confit_tui_schema_validation_message(message, message_size,
                                           "invalid schema option type");
      return 1;
    }
    return 0;
  case CONFIT_TUI_SCHEMA_FIELD_DEFAULT: {
    ConfitValue value;
    ConfitStatus status;

    if (validator->option == 0) {
      confit_tui_schema_validation_message(message, message_size,
                                           "select an option first");
      return 1;
    }
    confit_value_init(&value);
    status = confit_tui_schema_parse_default_value(
        validator->option, text, &value, message, message_size);
    confit_value_clear(&value);
    return status == CONFIT_OK ? 0 : 1;
  }
  case CONFIT_TUI_SCHEMA_FIELD_PROMPT:
  case CONFIT_TUI_SCHEMA_FIELD_HELP:
  case CONFIT_TUI_SCHEMA_FIELD_CATEGORY:
    if (confit_tui_schema_text_has_control(text)) {
      confit_tui_schema_validation_message(message, message_size,
                                           "field contains control chars");
      return 1;
    }
    return 0;
  case CONFIT_TUI_SCHEMA_FIELD_TAGS:
    return confit_tui_schema_csv_validate(text, 1, message, message_size);
  case CONFIT_TUI_SCHEMA_FIELD_RANGE:
    if (validator->option == 0) {
      confit_tui_schema_validation_message(message, message_size,
                                           "select an option first");
      return 1;
    }
    if (text == 0 || text[0] == '\0') {
      return 0;
    }
    {
      ConfitTuiSchemaOption candidate = *validator->option;
      (void)snprintf(candidate.range, sizeof(candidate.range), "%s", text);
      candidate.range[sizeof(candidate.range) - 1U] = '\0';
      if (confit_tui_schema_validate_range(&candidate, 0) != CONFIT_OK) {
        confit_tui_schema_validation_message(
            message, message_size, "schema range does not contain the default");
        return 1;
      }
    }
    return 0;
  case CONFIT_TUI_SCHEMA_FIELD_CHOICES:
    if (validator->option == 0 ||
        strcmp(validator->option->type, "enum") != 0) {
      confit_tui_schema_validation_message(message, message_size,
                                           "choices only valid for enum");
      return 1;
    }
    if (confit_tui_schema_csv_validate(text, 0, message, message_size) != 0) {
      return 1;
    }
    if (validator->option->default_value[0] != '\0') {
      ConfitTuiSchemaOption candidate = *validator->option;
      ConfitValue value;
      ConfitStatus status;

      (void)snprintf(candidate.choices, sizeof(candidate.choices), "%s", text);
      candidate.choices[sizeof(candidate.choices) - 1U] = '\0';
      confit_value_init(&value);
      status = confit_tui_schema_parse_default_value(
          &candidate, candidate.default_value, &value, message, message_size);
      confit_value_clear(&value);
      if (status != CONFIT_OK) {
        return 1;
      }
    }
    return 0;
  default:
    break;
  }
  confit_tui_schema_validation_message(message, message_size,
                                       "invalid schema field");
  return 1;
}

static int confit_tui_schema_read_field(const ConfitTuiSchemaState *state,
                                        const ConfitTuiSchemaOption *option,
                                        ConfitTuiSchemaField field,
                                        const char *field_name,
                                        const char *current, char *out,
                                        size_t out_size) {
  ConfitTuiSchemaFieldValidator validator;
  char header[512];
  char prompt[96];

  if (out == 0 || out_size == 0U) {
    return -1;
  }
  out[0] = '\0';
  (void)snprintf(
      header, sizeof(header),
      "SCHEMA EDIT MODE\nfield=%s\noption=%s\ncurrent=%s\nEnter commits a "
      "valid schema field. Esc cancels.",
      confit_tui_text_or_dash(field_name),
      confit_tui_text_or_dash(option != 0 ? option->id : "<new option>"),
      confit_tui_text_or_dash(current));
  header[sizeof(header) - 1U] = '\0';
  (void)snprintf(prompt, sizeof(prompt),
                 "%s: ", confit_tui_text_or_dash(field_name));
  prompt[sizeof(prompt) - 1U] = '\0';
  validator.state = state;
  validator.option = option;
  validator.field = field;
  return confit_tui_curses_read_value_dialog(
      "Confit Schema Field", header, prompt, "Enter a schema field value",
      confit_tui_schema_field_validator, &validator, out, out_size);
}

static ConfitStatus confit_tui_schema_add_option(ConfitTuiSchemaState *state,
                                                 ConfitDiagnostic *diagnostic) {
  char id[128];
  char type[32];
  char prompt[128];
  ConfitTuiSchemaOption *new_options;
  ConfitTuiSchemaOption *option;
  int input_status;

  input_status = confit_tui_schema_read_field(
      state, 0, CONFIT_TUI_SCHEMA_FIELD_ID, "option id", "-", id, sizeof(id));
  if (input_status != 0 || id[0] == '\0') {
    if (input_status > 0) {
      (void)snprintf(state->status, sizeof(state->status), "cancelled");
      state->status[sizeof(state->status) - 1U] = '\0';
    }
    return CONFIT_OK;
  }
  input_status = confit_tui_schema_read_field(
      state, 0, CONFIT_TUI_SCHEMA_FIELD_TYPE, "type",
      "bool|int|uint|hex|string|enum|float|path", type, sizeof(type));
  if (input_status != 0 || type[0] == '\0') {
    if (input_status > 0) {
      (void)snprintf(state->status, sizeof(state->status), "cancelled");
      state->status[sizeof(state->status) - 1U] = '\0';
    }
    return CONFIT_OK;
  }
  input_status =
      confit_tui_schema_read_field(state, 0, CONFIT_TUI_SCHEMA_FIELD_PROMPT,
                                   "prompt", "-", prompt, sizeof(prompt));
  if (input_status != 0) {
    if (input_status > 0) {
      (void)snprintf(state->status, sizeof(state->status), "cancelled");
      state->status[sizeof(state->status) - 1U] = '\0';
      return CONFIT_OK;
    }
    prompt[0] = '\0';
  }

  new_options = (ConfitTuiSchemaOption *)realloc(
      state->options_list,
      (state->option_count + 1U) * sizeof(state->options_list[0]));
  if (new_options == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, id, 0, 0,
                          "failed to allocate schema option");
    return CONFIT_ERR_INTERNAL;
  }
  state->options_list = new_options;
  option = &state->options_list[state->option_count];
  memset(option, 0, sizeof(*option));
  confit_tui_schema_copy_text(option->id, sizeof(option->id), id);
  confit_tui_schema_copy_text(option->type, sizeof(option->type), type);
  confit_tui_schema_copy_text(option->prompt, sizeof(option->prompt), prompt);
  confit_tui_schema_default_for_type(option);
  state->selected_index = state->option_count;
  state->option_count += 1U;
  state->dirty = 1;
  (void)snprintf(state->status, sizeof(state->status), "created %s", id);
  state->status[sizeof(state->status) - 1U] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_tui_schema_set_string(char *slot, size_t slot_size,
                                                 const char *field_name,
                                                 ConfitTuiSchemaField field,
                                                 ConfitTuiSchemaOption *option,
                                                 ConfitTuiSchemaState *state) {
  char input[256];
  int input_status;

  input_status = confit_tui_schema_read_field(state, option, field, field_name,
                                              slot != 0 ? slot : "-", input,
                                              sizeof(input));
  if (input_status != 0) {
    if (input_status > 0) {
      (void)snprintf(state->status, sizeof(state->status), "cancelled");
      state->status[sizeof(state->status) - 1U] = '\0';
    }
    return CONFIT_OK;
  }
  confit_tui_schema_copy_text(slot, slot_size, input);
  state->dirty = 1;
  (void)snprintf(state->status, sizeof(state->status), "schema field updated");
  state->status[sizeof(state->status) - 1U] = '\0';
  return CONFIT_OK;
}

static void
confit_tui_schema_clear_incompatible_fields(ConfitTuiSchemaOption *option) {
  const int is_numeric =
      strcmp(option->type, "int") == 0 || strcmp(option->type, "uint") == 0 ||
      strcmp(option->type, "hex") == 0 || strcmp(option->type, "float") == 0;

  if (strcmp(option->type, "enum") != 0) {
    option->choices[0] = '\0';
  }
  if (!is_numeric) {
    option->range[0] = '\0';
  }
}

static ConfitStatus confit_tui_schema_set_type(ConfitTuiSchemaState *state,
                                               ConfitTuiSchemaOption *option) {
  char input[32];
  int input_status;

  input_status = confit_tui_schema_read_field(
      state, option, CONFIT_TUI_SCHEMA_FIELD_TYPE, "type",
      "bool|int|uint|hex|string|enum|float|path", input, sizeof(input));
  if (input_status != 0) {
    if (input_status > 0) {
      (void)snprintf(state->status, sizeof(state->status), "cancelled");
      state->status[sizeof(state->status) - 1U] = '\0';
    }
    return CONFIT_OK;
  }
  if (strcmp(option->type, input) == 0) {
    (void)snprintf(state->status, sizeof(state->status), "schema type kept");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  confit_tui_schema_copy_text(option->type, sizeof(option->type), input);
  confit_tui_schema_default_for_type(option);
  confit_tui_schema_clear_incompatible_fields(option);
  state->dirty = 1;
  (void)snprintf(state->status, sizeof(state->status),
                 "schema type updated; default reset");
  state->status[sizeof(state->status) - 1U] = '\0';
  return CONFIT_OK;
}

static ConfitStatus
confit_tui_schema_validate_range(const ConfitTuiSchemaOption *option,
                                 ConfitDiagnostic *diagnostic) {
  char scratch[128];
  char *comma;
  uint64_t uint_min;
  uint64_t uint_max;
  uint64_t uint_default;
  int64_t int_min;
  int64_t int_max;
  int64_t int_default;
  double float_min;
  double float_max;
  double float_default;

  if (option->range[0] == '\0') {
    return CONFIT_OK;
  }
  if (strcmp(option->type, "int") != 0 && strcmp(option->type, "uint") != 0 &&
      strcmp(option->type, "hex") != 0 && strcmp(option->type, "float") != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                          "range is only valid for numeric schema options");
    return CONFIT_ERR_SCHEMA;
  }
  (void)snprintf(scratch, sizeof(scratch), "%s", option->range);
  scratch[sizeof(scratch) - 1U] = '\0';
  comma = strchr(scratch, ',');
  if (comma == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                          "schema range must use min,max");
    return CONFIT_ERR_SCHEMA;
  }
  *comma = '\0';
  if (strcmp(option->type, "int") == 0) {
    if (confit_tui_parse_int64(scratch, &int_min) == CONFIT_OK &&
        confit_tui_parse_int64(comma + 1, &int_max) == CONFIT_OK &&
        confit_tui_parse_int64(option->default_value, &int_default) ==
            CONFIT_OK &&
        int_min <= int_max && int_default >= int_min &&
        int_default <= int_max) {
      return CONFIT_OK;
    }
  }
  if (strcmp(option->type, "float") == 0 &&
      confit_tui_parse_double(scratch, &float_min) == CONFIT_OK &&
      confit_tui_parse_double(comma + 1, &float_max) == CONFIT_OK &&
      confit_tui_parse_double(option->default_value, &float_default) ==
          CONFIT_OK &&
      float_min <= float_max && float_default >= float_min &&
      float_default <= float_max) {
    return CONFIT_OK;
  }
  if ((strcmp(option->type, "uint") == 0 || strcmp(option->type, "hex") == 0) &&
      confit_tui_parse_uint64(scratch, &uint_min) == CONFIT_OK &&
      confit_tui_parse_uint64(comma + 1, &uint_max) == CONFIT_OK &&
      confit_tui_parse_uint64(option->default_value, &uint_default) ==
          CONFIT_OK &&
      uint_min <= uint_max && uint_default >= uint_min &&
      uint_default <= uint_max) {
    return CONFIT_OK;
  }
  confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                        "schema range does not contain the default");
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus confit_tui_schema_csv_each(
    const char *csv, ConfitStatus (*callback)(const char *token, void *user),
    void *user) {
  char scratch[256];
  char *cursor;
  char *token;
  ConfitStatus status;

  if (csv == 0 || csv[0] == '\0') {
    return CONFIT_OK;
  }
  (void)snprintf(scratch, sizeof(scratch), "%s", csv);
  scratch[sizeof(scratch) - 1U] = '\0';
  cursor = scratch;
  while ((token = strtok(cursor, ",")) != 0) {
    char *end;

    cursor = 0;
    while (*token == ' ' || *token == '\t') {
      token += 1;
    }
    end = token + strlen(token);
    while (end > token && (end[-1] == ' ' || end[-1] == '\t')) {
      end -= 1;
    }
    *end = '\0';
    status = callback(token, user);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_tui_schema_add_tag_callback(const char *token,
                                                       void *user) {
  return confit_option_add_tag((ConfitOption *)user, token);
}

static ConfitStatus confit_tui_schema_add_enum_callback(const char *token,
                                                        void *user) {
  return confit_option_add_enum_value((ConfitOption *)user, token);
}

static ConfitStatus
confit_tui_schema_parse_range_value(const ConfitTuiSchemaOption *option,
                                    const char *text, ConfitValue *value) {
  if (strcmp(option->type, "int") == 0) {
    int64_t parsed;
    if (confit_tui_parse_int64(text, &parsed) != CONFIT_OK) {
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_int(value, parsed);
    return CONFIT_OK;
  }
  if (strcmp(option->type, "uint") == 0 || strcmp(option->type, "hex") == 0) {
    uint64_t parsed;
    if (confit_tui_parse_uint64(text, &parsed) != CONFIT_OK) {
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_uint(value, parsed);
    return CONFIT_OK;
  }
  if (strcmp(option->type, "float") == 0) {
    double parsed;
    if (confit_tui_parse_double(text, &parsed) != CONFIT_OK) {
      return CONFIT_ERR_SCHEMA;
    }
    confit_value_set_float(value, parsed);
    return CONFIT_OK;
  }
  return CONFIT_ERR_SCHEMA;
}

static ConfitStatus
confit_tui_schema_set_model_range(const ConfitTuiSchemaOption *draft,
                                  ConfitOption *option) {
  char scratch[128];
  char *comma;
  ConfitValue min_value;
  ConfitValue max_value;
  ConfitStatus status;

  if (draft->range[0] == '\0') {
    return CONFIT_OK;
  }
  (void)snprintf(scratch, sizeof(scratch), "%s", draft->range);
  scratch[sizeof(scratch) - 1U] = '\0';
  comma = strchr(scratch, ',');
  if (comma == 0) {
    return CONFIT_ERR_SCHEMA;
  }
  *comma = '\0';
  confit_value_init(&min_value);
  confit_value_init(&max_value);
  status = confit_tui_schema_parse_range_value(draft, scratch, &min_value);
  if (status == CONFIT_OK) {
    status = confit_tui_schema_parse_range_value(draft, comma + 1, &max_value);
  }
  if (status == CONFIT_OK) {
    status = confit_option_set_range(option, &min_value, &max_value);
  }
  confit_value_clear(&min_value);
  confit_value_clear(&max_value);
  return status;
}

static ConfitStatus
confit_tui_schema_option_to_model(const ConfitTuiSchemaOption *draft,
                                  ConfitOption *option,
                                  ConfitDiagnostic *diagnostic) {
  ConfitValue default_value;
  ConfitStatus status;
  char default_text[128];
  char message[160];

  message[0] = '\0';
  status = confit_option_set_identity(
      option, draft->id, confit_tui_schema_type_from_string(draft->type));
  if (status == CONFIT_OK) {
    status = confit_option_set_metadata(
        option, draft->prompt[0] != '\0' ? draft->prompt : 0,
        draft->category[0] != '\0' ? draft->category : 0,
        draft->help[0] != '\0' ? draft->help : 0);
  }
  if (status == CONFIT_OK && draft->tags[0] != '\0') {
    status = confit_tui_schema_csv_each(
        draft->tags, confit_tui_schema_add_tag_callback, option);
  }
  if (status == CONFIT_OK && strcmp(draft->type, "enum") == 0) {
    if (draft->choices[0] == '\0') {
      status = CONFIT_ERR_SCHEMA;
    } else {
      status = confit_tui_schema_csv_each(
          draft->choices, confit_tui_schema_add_enum_callback, option);
    }
  }
  if (status == CONFIT_OK) {
    confit_value_init(&default_value);
    if (strcmp(draft->type, "enum") == 0 && draft->default_value[0] == '\0') {
      status = confit_tui_schema_first_choice(draft->choices, default_text,
                                              sizeof(default_text));
    } else {
      (void)snprintf(default_text, sizeof(default_text), "%s",
                     draft->default_value);
      default_text[sizeof(default_text) - 1U] = '\0';
    }
    if (status == CONFIT_OK) {
      status = confit_tui_schema_parse_default_value(
          draft, default_text, &default_value, message, sizeof(message));
    }
    if (status == CONFIT_OK) {
      status = confit_option_set_default(option, &default_value);
    }
    confit_value_clear(&default_value);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_schema_set_model_range(draft, option);
  }
  if (status == CONFIT_OK) {
    status = confit_option_validate_default(option);
  }
  if (status != CONFIT_OK) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, draft->id, 0, 0,
                          message[0] != '\0' ? message
                                             : "invalid schema draft option");
    return CONFIT_ERR_SCHEMA;
  }
  return CONFIT_OK;
}

static ConfitStatus
confit_tui_schema_validate_draft_model(const ConfitTuiSchemaState *state,
                                       ConfitDiagnostic *diagnostic) {
  ConfitProject *draft_project;
  ConfitGraph *draft_graph;
  ConfitStatus status;
  size_t index;

  draft_project = confit_project_create();
  draft_graph = 0;
  if (draft_project == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          "failed to allocate schema validation project");
    return CONFIT_ERR_INTERNAL;
  }
  status = confit_project_set_identity(draft_project, "schema-drafts", 0, 1U);
  for (index = 0U; status == CONFIT_OK && index < state->option_count;
       ++index) {
    ConfitOption *option;

    status = confit_project_add_option(draft_project, &option);
    if (status == CONFIT_OK) {
      status = confit_tui_schema_option_to_model(&state->options_list[index],
                                                 option, diagnostic);
    }
  }
  if (status == CONFIT_OK) {
    status = confit_graph_build(draft_project, &draft_graph, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_graph_validate(draft_graph, diagnostic);
  }
  confit_graph_free(draft_graph);
  confit_project_free(draft_project);
  return status;
}

static ConfitStatus
confit_tui_schema_validate_drafts(const ConfitTuiSchemaState *state,
                                  ConfitDiagnostic *diagnostic) {
  size_t index;

  for (index = 0U; index < state->option_count; ++index) {
    const ConfitTuiSchemaOption *option = &state->options_list[index];
    size_t next_index;

    if (!confit_tui_schema_valid_option_id(option->id) ||
        !confit_tui_schema_type_is_valid(option->type)) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                            "invalid schema draft");
      return CONFIT_ERR_SCHEMA;
    }
    for (next_index = index + 1U; next_index < state->option_count;
         ++next_index) {
      if (strcmp(option->id, state->options_list[next_index].id) == 0) {
        confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                              "duplicate schema draft id");
        return CONFIT_ERR_SCHEMA;
      }
    }
    if (strcmp(option->type, "enum") == 0 && option->choices[0] == '\0') {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                            "enum schema option needs choices");
      return CONFIT_ERR_SCHEMA;
    }
    if (strcmp(option->type, "enum") != 0 && option->choices[0] != '\0') {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, option->id, 0, 0,
                            "choices only valid for enum");
      return CONFIT_ERR_SCHEMA;
    }
    if (confit_tui_schema_validate_range(option, diagnostic) != CONFIT_OK) {
      return CONFIT_ERR_SCHEMA;
    }
  }
  return confit_tui_schema_validate_draft_model(state, diagnostic);
}

static ConfitStatus
confit_tui_text_append_csv_quoted_array(ConfitTuiTextBuilder *builder,
                                        const char *csv) {
  char scratch[256];
  char *cursor;
  char *token;
  int first;
  ConfitStatus status;

  status = confit_tui_text_append(builder, "[");
  if (status != CONFIT_OK) {
    return status;
  }
  (void)snprintf(scratch, sizeof(scratch), "%s", csv != 0 ? csv : "");
  scratch[sizeof(scratch) - 1U] = '\0';
  cursor = scratch;
  first = 1;
  while ((token = strtok(cursor, ",")) != 0) {
    cursor = 0;
    while (*token == ' ' || *token == '\t') {
      token += 1;
    }
    if (!first) {
      status = confit_tui_text_append(builder, ", ");
      if (status != CONFIT_OK) {
        return status;
      }
    }
    status = confit_tui_text_append_quoted(builder, token);
    if (status != CONFIT_OK) {
      return status;
    }
    first = 0;
  }
  return confit_tui_text_append(builder, "]");
}

static ConfitStatus
confit_tui_schema_append_default(ConfitTuiTextBuilder *builder,
                                 const ConfitTuiSchemaOption *option) {
  if (strcmp(option->type, "enum") == 0) {
    char first_choice[128];
    char *comma;

    if (option->default_value[0] != '\0') {
      return confit_tui_text_append_quoted(builder, option->default_value);
    }
    (void)snprintf(first_choice, sizeof(first_choice), "%s", option->choices);
    first_choice[sizeof(first_choice) - 1U] = '\0';
    comma = strchr(first_choice, ',');
    if (comma != 0) {
      *comma = '\0';
    }
    return confit_tui_text_append_quoted(builder, first_choice);
  }
  if (strcmp(option->type, "string") == 0 ||
      strcmp(option->type, "path") == 0) {
    return confit_tui_text_append_quoted(builder, option->default_value);
  }
  return confit_tui_text_append(builder, option->default_value);
}

static ConfitStatus
confit_tui_schema_build_toml(const ConfitTuiSchemaState *state, char **out_text,
                             ConfitDiagnostic *diagnostic) {
  ConfitTuiTextBuilder builder;
  ConfitStatus status;
  size_t index;

  *out_text = 0;
  status = confit_tui_schema_validate_drafts(state, diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  confit_tui_text_builder_init(&builder);
  for (index = 0U; status == CONFIT_OK && index < state->option_count;
       ++index) {
    const ConfitTuiSchemaOption *option = &state->options_list[index];

    status = confit_tui_text_append(&builder, "[option.");
    if (status == CONFIT_OK) {
      status = confit_tui_text_append_quoted(&builder, option->id);
    }
    if (status == CONFIT_OK) {
      status = confit_tui_text_append(&builder, "]\ntype = ");
    }
    if (status == CONFIT_OK) {
      status = confit_tui_text_append_quoted(&builder, option->type);
    }
    if (status == CONFIT_OK && option->choices[0] != '\0') {
      status = confit_tui_text_append(&builder, "\nchoices = ");
      if (status == CONFIT_OK) {
        status =
            confit_tui_text_append_csv_quoted_array(&builder, option->choices);
      }
    }
    if (status == CONFIT_OK) {
      status = confit_tui_text_append(&builder, "\ndefault = ");
    }
    if (status == CONFIT_OK) {
      status = confit_tui_schema_append_default(&builder, option);
    }
    if (status == CONFIT_OK && option->prompt[0] != '\0') {
      status = confit_tui_text_append(&builder, "\nprompt = ");
      if (status == CONFIT_OK) {
        status = confit_tui_text_append_quoted(&builder, option->prompt);
      }
    }
    if (status == CONFIT_OK && option->category[0] != '\0') {
      status = confit_tui_text_append(&builder, "\ncategory = ");
      if (status == CONFIT_OK) {
        status = confit_tui_text_append_quoted(&builder, option->category);
      }
    }
    if (status == CONFIT_OK && option->tags[0] != '\0') {
      status = confit_tui_text_append(&builder, "\ntags = ");
      if (status == CONFIT_OK) {
        status =
            confit_tui_text_append_csv_quoted_array(&builder, option->tags);
      }
    }
    if (status == CONFIT_OK && option->help[0] != '\0') {
      status = confit_tui_text_append(&builder, "\nhelp = ");
      if (status == CONFIT_OK) {
        status = confit_tui_text_append_quoted(&builder, option->help);
      }
    }
    if (status == CONFIT_OK && option->range[0] != '\0') {
      status = confit_tui_text_append(&builder, "\nrange = [");
      if (status == CONFIT_OK) {
        status = confit_tui_text_append(&builder, option->range);
      }
      if (status == CONFIT_OK) {
        status = confit_tui_text_append(&builder, "]");
      }
    }
    if (status == CONFIT_OK) {
      status = confit_tui_text_append(
          &builder, index + 1U < state->option_count ? "\n\n" : "\n");
    }
  }
  if (status != CONFIT_OK) {
    free(builder.text);
    return status;
  }
  *out_text = builder.text;
  return CONFIT_OK;
}

static ConfitStatus confit_tui_schema_path(const ConfitTuiOptions *options,
                                           char *out_path, size_t out_size,
                                           char *out_dir, size_t out_dir_size,
                                           ConfitDiagnostic *diagnostic) {
  char config_dir[1024];
  ConfitStatus status;

  status = confit_host_path_join(config_dir, sizeof(config_dir),
                                 options->project_root, "config", diagnostic);
  if (status == CONFIT_OK) {
    status = confit_host_path_join(out_dir, out_dir_size, config_dir, "options",
                                   diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_path_join(out_path, out_size, out_dir,
                                   "tui-schema.toml", diagnostic);
  }
  return status;
}

static ConfitStatus confit_tui_schema_save(ConfitTuiSchemaState *state,
                                           ConfitDiagnostic *diagnostic) {
  char *toml;
  char schema_dir[1024];
  char schema_path[1024];
  ConfitProject *check_project;
  ConfitGraph *check_graph;
  ConfitStatus status;

  toml = 0;
  check_project = 0;
  check_graph = 0;
  status = confit_tui_schema_build_toml(state, &toml, diagnostic);
  if (status == CONFIT_OK) {
    status =
        confit_tui_schema_path(state->options, schema_path, sizeof(schema_path),
                               schema_dir, sizeof(schema_dir), diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_make_directories(schema_dir, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_write_text_file(schema_path, toml, diagnostic);
  }
  free(toml);
  if (status == CONFIT_OK) {
    status = confit_schema_load_project(state->options->project_root,
                                        &check_project, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_graph_build(check_project, &check_graph, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_graph_validate(check_graph, diagnostic);
  }
  if (status == CONFIT_OK) {
    confit_graph_free(state->graph);
    confit_project_free(state->project);
    state->project = check_project;
    state->graph = check_graph;
    check_project = 0;
    check_graph = 0;
    state->dirty = 0;
    state->status[0] = '\0';
    confit_tui_schema_append_text(
        state->status, sizeof(state->status),
        "schema saved and validated; reloaded graph ");
    confit_tui_schema_append_text(state->status, sizeof(state->status),
                                  schema_path);
  }
  confit_graph_free(check_graph);
  confit_project_free(check_project);
  return status;
}

static ConfitStatus
confit_tui_schema_render(const ConfitTuiSchemaState *state) {
  ConfitTuiListItem *items;
  ConfitTuiScreen screen;
  char status_line[384];
  char header[256];
  char key_legend[128];
  char (*details)[224];
  char (*values)[64];
  size_t index;

  items = 0;
  details = 0;
  values = 0;
  if (state->option_count > 0U) {
    items = (ConfitTuiListItem *)calloc(state->option_count, sizeof(items[0]));
    details = (char (*)[224])calloc(state->option_count, sizeof(details[0]));
    values = (char (*)[64])calloc(state->option_count, sizeof(values[0]));
    if (items == 0 || details == 0 || values == 0) {
      free(items);
      free(details);
      free(values);
      return CONFIT_ERR_INTERNAL;
    }
  }
  for (index = 0U; index < state->option_count; ++index) {
    const ConfitTuiSchemaOption *option = &state->options_list[index];

    (void)snprintf(details[index], sizeof(details[index]),
                   "schema-edit | default=%s | prompt=%s | help=%s | "
                   "category=%s tags=%s range=%s choices=%s",
                   confit_tui_text_or_dash(option->default_value),
                   confit_tui_text_or_dash(option->prompt),
                   confit_tui_text_or_dash(option->help),
                   confit_tui_text_or_dash(option->category),
                   confit_tui_text_or_dash(option->tags),
                   confit_tui_text_or_dash(option->range),
                   confit_tui_text_or_dash(option->choices));
    details[index][sizeof(details[index]) - 1U] = '\0';
    (void)snprintf(values[index], sizeof(values[index]), "%s", option->type);
    values[index][sizeof(values[index]) - 1U] = '\0';
    items[index].label = option->id;
    items[index].detail = details[index];
    items[index].value = values[index];
  }

  (void)snprintf(
      header, sizeof(header),
      "SCHEMA EDIT MODE - guarded. Changes project configuration semantics.\n"
      "Danger: edits are schema authority, not profile values.\n"
      "project=%s dirty=%s",
      confit_tui_text_or_dash(state->project != 0 ? state->project->name : 0),
      state->dirty ? "yes" : "no");
  header[sizeof(header) - 1U] = '\0';
  (void)snprintf(key_legend, sizeof(key_legend),
                 "arrows/jk PgUp/PgDn Home/End n new y type d/e default p "
                 "prompt h help c/t/r/o fields ? keys s save q quit");
  key_legend[sizeof(key_legend) - 1U] = '\0';
  (void)snprintf(
      status_line, sizeof(status_line), "schema %lu/%lu | SCHEMA EDIT | %s",
      state->option_count == 0U ? 0UL
                                : (unsigned long)(state->selected_index + 1U),
      (unsigned long)state->option_count,
      state->status[0] != '\0' ? state->status : "guarded");
  status_line[sizeof(status_line) - 1U] = '\0';
  screen.title = "Confit Schema Editor - menuconfig guarded schema";
  screen.header = header;
  screen.key_legend = key_legend;
  screen.items = items;
  screen.item_count = state->option_count;
  screen.selected_index = state->selected_index;
  screen.status = status_line;
  if (confit_tui_curses_render(&screen) != 0) {
    free(items);
    free(details);
    free(values);
    return CONFIT_ERR_INTERNAL;
  }
  free(items);
  free(details);
  free(values);
  return CONFIT_OK;
}

static int confit_tui_schema_move_selection(ConfitTuiSchemaState *state,
                                            ConfitTuiKey key) {
  size_t step;

  switch (key) {
  case CONFIT_TUI_KEY_DOWN:
    if (state->option_count == 0U) {
      state->selected_index = 0U;
      return 1;
    }
    if (state->selected_index + 1U < state->option_count) {
      state->selected_index += 1U;
    }
    return 1;
  case CONFIT_TUI_KEY_UP:
    if (state->option_count == 0U) {
      state->selected_index = 0U;
      return 1;
    }
    if (state->selected_index > 0U) {
      state->selected_index -= 1U;
    }
    return 1;
  case CONFIT_TUI_KEY_PAGE_DOWN:
    if (state->option_count == 0U) {
      state->selected_index = 0U;
      return 1;
    }
    step = confit_tui_curses_page_step();
    if (state->selected_index + step < state->option_count) {
      state->selected_index += step;
    } else {
      state->selected_index = state->option_count - 1U;
    }
    (void)snprintf(state->status, sizeof(state->status), "moved PageDown");
    state->status[sizeof(state->status) - 1U] = '\0';
    return 1;
  case CONFIT_TUI_KEY_PAGE_UP:
    if (state->option_count == 0U) {
      state->selected_index = 0U;
      return 1;
    }
    step = confit_tui_curses_page_step();
    if (state->selected_index > step) {
      state->selected_index -= step;
    } else {
      state->selected_index = 0U;
    }
    (void)snprintf(state->status, sizeof(state->status), "moved PageUp");
    state->status[sizeof(state->status) - 1U] = '\0';
    return 1;
  case CONFIT_TUI_KEY_HOME:
    state->selected_index = 0U;
    (void)snprintf(state->status, sizeof(state->status), "moved Home");
    state->status[sizeof(state->status) - 1U] = '\0';
    return 1;
  case CONFIT_TUI_KEY_END:
    if (state->option_count == 0U) {
      state->selected_index = 0U;
      return 1;
    }
    state->selected_index = state->option_count - 1U;
    (void)snprintf(state->status, sizeof(state->status), "moved End");
    state->status[sizeof(state->status) - 1U] = '\0';
    return 1;
  default:
    break;
  }
  return 0;
}

static void confit_tui_schema_show_keymap(ConfitTuiSchemaState *state) {
  (void)snprintf(state->status, sizeof(state->status),
                 "keys: arrows/jk PgUp/PgDn Home/End n new y type d/e "
                 "default p prompt h help c/t/r/o fields ? keys Esc cancel q "
                 "quit");
  state->status[sizeof(state->status) - 1U] = '\0';
}

static ConfitStatus confit_tui_schema_handle_key(ConfitTuiSchemaState *state,
                                                 ConfitTuiKey key,
                                                 ConfitDiagnostic *diagnostic) {
  ConfitTuiSchemaOption *option;

  if (confit_tui_schema_move_selection(state, key)) {
    return CONFIT_OK;
  }
  if (key == CONFIT_TUI_KEY_KEYMAP_HELP) {
    confit_tui_schema_show_keymap(state);
    return CONFIT_OK;
  }
  if (key == CONFIT_TUI_KEY_CANCEL) {
    (void)snprintf(state->status, sizeof(state->status), "cancelled");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  if (key == CONFIT_TUI_KEY_NEW) {
    return confit_tui_schema_add_option(state, diagnostic);
  }
  if (key == CONFIT_TUI_KEY_SAVE) {
    return confit_tui_schema_save(state, diagnostic);
  }

  option = confit_tui_schema_selected(state);
  if (option == 0) {
    (void)snprintf(state->status, sizeof(state->status),
                   "create an option first");
    state->status[sizeof(state->status) - 1U] = '\0';
    return CONFIT_OK;
  }
  if (key == CONFIT_TUI_KEY_PROMPT) {
    return confit_tui_schema_set_string(
        option->prompt, sizeof(option->prompt), "prompt",
        CONFIT_TUI_SCHEMA_FIELD_PROMPT, option, state);
  }
  if (key == CONFIT_TUI_KEY_TYPE) {
    return confit_tui_schema_set_type(state, option);
  }
  if (key == CONFIT_TUI_KEY_HELP) {
    return confit_tui_schema_set_string(option->help, sizeof(option->help),
                                        "help", CONFIT_TUI_SCHEMA_FIELD_HELP,
                                        option, state);
  }
  if (key == CONFIT_TUI_KEY_CATEGORY) {
    return confit_tui_schema_set_string(
        option->category, sizeof(option->category), "category",
        CONFIT_TUI_SCHEMA_FIELD_CATEGORY, option, state);
  }
  if (key == CONFIT_TUI_KEY_TAG) {
    return confit_tui_schema_set_string(
        option->tags, sizeof(option->tags), "tags comma-list",
        CONFIT_TUI_SCHEMA_FIELD_TAGS, option, state);
  }
  if (key == CONFIT_TUI_KEY_RANGE) {
    return confit_tui_schema_set_string(
        option->range, sizeof(option->range), "range min,max",
        CONFIT_TUI_SCHEMA_FIELD_RANGE, option, state);
  }
  if (key == CONFIT_TUI_KEY_CHOICES) {
    return confit_tui_schema_set_string(
        option->choices, sizeof(option->choices), "choices comma-list",
        CONFIT_TUI_SCHEMA_FIELD_CHOICES, option, state);
  }
  if (key == CONFIT_TUI_KEY_DEFAULT || key == CONFIT_TUI_KEY_EDIT ||
      key == CONFIT_TUI_KEY_ENTER) {
    return confit_tui_schema_set_string(
        option->default_value, sizeof(option->default_value), "default",
        CONFIT_TUI_SCHEMA_FIELD_DEFAULT, option, state);
  }
  return CONFIT_OK;
}

static int confit_tui_schema_confirm_entry(const ConfitTuiSchemaState *state) {
  static const char *items[] = {"Enter schema editor", "Cancel"};
  char header[640];
  size_t selected_index;
  int select_status;

  (void)snprintf(
      header, sizeof(header),
      "SCHEMA EDIT MODE is a guarded workflow.\n"
      "Project: %s\n"
      "Schema changes alter configuration semantics for every profile.\n"
      "Use this mode only for deliberate schema work, then review the TOML.\n"
      "Generated outputs are not written here; only source TOML changes after "
      "validation.",
      confit_tui_text_or_dash(
          state != 0 && state->project != 0 ? state->project->name : 0));
  header[sizeof(header) - 1U] = '\0';
  selected_index = 0U;
  select_status =
      confit_tui_curses_select_dialog("Schema Edit Warning", header, items, 2U,
                                      selected_index, &selected_index);
  return select_status == 0 && selected_index == 0U;
}

ConfitStatus confit_tui_run_schema_editor(const ConfitTuiOptions *options,
                                          ConfitDiagnostic *diagnostic) {
  ConfitTuiSchemaState state;
  ConfitStatus status;
  ConfitTuiKey key;

  memset(&state, 0, sizeof(state));
  state.options = options;
  status = confit_tui_schema_load(&state, diagnostic);
  if (status != CONFIT_OK) {
    confit_graph_free(state.graph);
    confit_project_free(state.project);
    return status;
  }
  if (!confit_tui_schema_confirm_entry(&state)) {
    free(state.options_list);
    confit_graph_free(state.graph);
    confit_project_free(state.project);
    confit_tui_curses_stop();
    return CONFIT_OK;
  }
  (void)snprintf(state.status, sizeof(state.status), "guarded schema editing");
  do {
    status = confit_tui_schema_render(&state);
    if (status != CONFIT_OK) {
      break;
    }
    key = confit_tui_curses_read_key();
    if (key == CONFIT_TUI_KEY_QUIT) {
      break;
    }
    confit_diagnostic_init(diagnostic);
    status = confit_tui_schema_handle_key(&state, key, diagnostic);
    if (status != CONFIT_OK) {
      confit_tui_schema_set_status_from_diagnostic(&state, "error", status,
                                                   diagnostic);
      status = CONFIT_OK;
    }
  } while (1);

  free(state.options_list);
  confit_graph_free(state.graph);
  confit_project_free(state.project);
  confit_tui_curses_stop();
  return status;
}
