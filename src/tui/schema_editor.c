#include "tui_internal.h"

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

static ConfitStatus confit_tui_schema_add_option(ConfitTuiSchemaState *state,
                                                 ConfitDiagnostic *diagnostic) {
  char id[128];
  char type[32];
  char prompt[128];
  ConfitTuiSchemaOption *new_options;
  ConfitTuiSchemaOption *option;
  size_t index;

  if (confit_tui_curses_read_line("option id: ", id, sizeof(id)) != 0 ||
      id[0] == '\0') {
    return CONFIT_OK;
  }
  if (!confit_tui_schema_valid_option_id(id)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, id, 0, 0,
                          "invalid schema option id");
    return CONFIT_ERR_SCHEMA;
  }
  if (confit_project_find_option(state->project, id) != 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, id, 0, 0,
                          "schema option already exists");
    return CONFIT_ERR_SCHEMA;
  }
  for (index = 0U; index < state->option_count; ++index) {
    if (strcmp(state->options_list[index].id, id) == 0) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, id, 0, 0,
                            "schema draft already exists");
      return CONFIT_ERR_SCHEMA;
    }
  }
  if (confit_tui_curses_read_line("type: ", type, sizeof(type)) != 0 ||
      type[0] == '\0') {
    return CONFIT_OK;
  }
  if (!confit_tui_schema_type_is_valid(type)) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, type, 0, 0,
                          "invalid schema option type");
    return CONFIT_ERR_SCHEMA;
  }
  if (confit_tui_curses_read_line("prompt: ", prompt, sizeof(prompt)) != 0) {
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
  (void)snprintf(option->id, sizeof(option->id), "%s", id);
  (void)snprintf(option->type, sizeof(option->type), "%s", type);
  (void)snprintf(option->prompt, sizeof(option->prompt), "%s", prompt);
  confit_tui_schema_default_for_type(option);
  state->selected_index = state->option_count;
  state->option_count += 1U;
  state->dirty = 1;
  (void)snprintf(state->status, sizeof(state->status), "created %s", id);
  state->status[sizeof(state->status) - 1U] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_tui_schema_set_string(char *slot, size_t slot_size,
                                                 const char *prompt,
                                                 ConfitTuiSchemaState *state) {
  char input[256];

  if (confit_tui_curses_read_line(prompt, input, sizeof(input)) != 0) {
    return CONFIT_OK;
  }
  (void)snprintf(slot, slot_size, "%s", input);
  slot[slot_size - 1U] = '\0';
  state->dirty = 1;
  (void)snprintf(state->status, sizeof(state->status), "schema field updated");
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
    if (confit_tui_schema_validate_range(option, diagnostic) != CONFIT_OK) {
      return CONFIT_ERR_SCHEMA;
    }
  }
  return CONFIT_OK;
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
  confit_graph_free(check_graph);
  confit_project_free(check_project);
  if (status == CONFIT_OK) {
    state->dirty = 0;
    (void)snprintf(state->status, sizeof(state->status), "schema saved %s",
                   schema_path);
    state->status[sizeof(state->status) - 1U] = '\0';
  }
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
                   "%s | %s | %s | tags=%s range=%s choices=%s",
                   confit_tui_text_or_dash(option->category),
                   confit_tui_text_or_dash(option->prompt),
                   confit_tui_text_or_dash(option->help),
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
      "Schema edit mode changes project configuration semantics.\n"
      "Prefer code review for schema changes.\n"
      "project=%s dirty=%s",
      confit_tui_text_or_dash(state->project != 0 ? state->project->name : 0),
      state->dirty ? "yes" : "no");
  header[sizeof(header) - 1U] = '\0';
  (void)snprintf(key_legend, sizeof(key_legend),
                 "n new p prompt h help c category t tag r range o choices "
                 "s save q quit");
  key_legend[sizeof(key_legend) - 1U] = '\0';
  (void)snprintf(status_line, sizeof(status_line), "schema %lu/%lu | %s",
                 state->option_count == 0U
                     ? 0UL
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

static ConfitStatus confit_tui_schema_handle_key(ConfitTuiSchemaState *state,
                                                 ConfitTuiKey key,
                                                 ConfitDiagnostic *diagnostic) {
  ConfitTuiSchemaOption *option;

  if (key == CONFIT_TUI_KEY_DOWN &&
      state->selected_index + 1U < state->option_count) {
    state->selected_index += 1U;
    return CONFIT_OK;
  }
  if (key == CONFIT_TUI_KEY_UP && state->selected_index > 0U) {
    state->selected_index -= 1U;
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
    return confit_tui_schema_set_string(option->prompt, sizeof(option->prompt),
                                        "prompt: ", state);
  }
  if (key == CONFIT_TUI_KEY_HELP) {
    return confit_tui_schema_set_string(option->help, sizeof(option->help),
                                        "help: ", state);
  }
  if (key == CONFIT_TUI_KEY_CATEGORY) {
    return confit_tui_schema_set_string(
        option->category, sizeof(option->category), "category: ", state);
  }
  if (key == CONFIT_TUI_KEY_TAG) {
    return confit_tui_schema_set_string(option->tags, sizeof(option->tags),
                                        "tags comma-list: ", state);
  }
  if (key == CONFIT_TUI_KEY_RANGE) {
    return confit_tui_schema_set_string(option->range, sizeof(option->range),
                                        "range min,max: ", state);
  }
  if (key == CONFIT_TUI_KEY_CHOICES) {
    return confit_tui_schema_set_string(option->choices,
                                        sizeof(option->choices),
                                        "choices comma-list: ", state);
  }
  return CONFIT_OK;
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
      (void)snprintf(state.status, sizeof(state.status), "error: %s",
                     diagnostic->message != 0 ? diagnostic->message
                                              : confit_status_name(status));
      state.status[sizeof(state.status) - 1U] = '\0';
      status = CONFIT_OK;
    }
  } while (1);

  free(state.options_list);
  confit_graph_free(state.graph);
  confit_project_free(state.project);
  confit_tui_curses_stop();
  return status;
}
