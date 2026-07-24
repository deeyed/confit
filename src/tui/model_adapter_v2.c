#include "tui_internal.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/generator_v2.h"
#include "confit/host.h"
#include "confit/link_v2.h"
#include "confit/parser_v2.h"
#include "confit/resolver_v2.h"
#include "confit/schema_v2.h"

typedef enum ConfitTuiV2RowKind {
  CONFIT_TUI_V2_ROW_MENU = 1,
  CONFIT_TUI_V2_ROW_OPTION,
} ConfitTuiV2RowKind;

typedef struct ConfitTuiV2Menu {
  const ConfitV2MenuNode *source;
  size_t parent_index;
} ConfitTuiV2Menu;

typedef struct ConfitTuiV2Edit {
  char *option_id;
  char *value_text;
} ConfitTuiV2Edit;

typedef struct ConfitTuiV2Row {
  ConfitTuiListItem item;
  ConfitTuiV2RowKind kind;
  const ConfitV2Symbol *symbol;
  size_t menu_index;
  char label[256];
  char detail[160];
  char value[192];
} ConfitTuiV2Row;

typedef struct ConfitTuiV2State {
  const ConfitTuiOptions *options;
  ConfitV2Project *project;
  ConfitV2LinkedProject *linked;
  ConfitV2CompiledStructure *compiled;
  ConfitV2Snapshot *snapshot;
  ConfitTuiV2Menu *menus;
  size_t menu_count;
  size_t current_menu_index;
  ConfitTuiV2Row *rows;
  size_t row_count;
  size_t selected_index;
  ConfitTuiV2Edit *edits;
  size_t edit_count;
  size_t *search_matches;
  size_t search_count;
  size_t search_position;
  char search[128];
  char text_filter[128];
  char status[256];
  int dirty;
  int verbose_inspector;
  int quit_requested;
  int flat_view;
} ConfitTuiV2State;

typedef struct ConfitTuiV2Validator {
  ConfitTuiV2State *state;
  const ConfitV2Symbol *symbol;
} ConfitTuiV2Validator;

static char kTuiV2TransactionSource[] = "tui profile transaction";

static int confit_tui_v2_symbol_matches(const ConfitV2Symbol *symbol,
                                         const char *query);

static char *confit_tui_v2_strdup(const char *text) {
  char *copy;
  size_t size;

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

static const char *confit_tui_v2_type_name(ConfitV2OptionType type) {
  switch (type) {
  case CONFIT_V2_OPTION_TYPE_BOOL:
    return "bool";
  case CONFIT_V2_OPTION_TYPE_TRISTATE:
    return "tristate";
  case CONFIT_V2_OPTION_TYPE_INT:
    return "int";
  case CONFIT_V2_OPTION_TYPE_UINT:
    return "uint";
  case CONFIT_V2_OPTION_TYPE_HEX:
    return "hex";
  case CONFIT_V2_OPTION_TYPE_FLOAT:
    return "float";
  case CONFIT_V2_OPTION_TYPE_STRING:
    return "string";
  case CONFIT_V2_OPTION_TYPE_ENUM:
    return "enum";
  case CONFIT_V2_OPTION_TYPE_PATH:
    return "path";
  case CONFIT_V2_OPTION_TYPE_STRING_LIST:
    return "string_list";
  case CONFIT_V2_OPTION_TYPE_PATH_LIST:
    return "path_list";
  case CONFIT_V2_OPTION_TYPE_ENUM_SET:
    return "enum_set";
  default:
    return "invalid";
  }
}

static const char *confit_tui_v2_domain_name(ConfitV2WriteDomain domain) {
  switch (domain) {
  case CONFIT_V2_WRITE_DOMAIN_SCHEMA:
    return "schema";
  case CONFIT_V2_WRITE_DOMAIN_PROFILE:
    return "profile";
  case CONFIT_V2_WRITE_DOMAIN_TARGET:
    return "target";
  case CONFIT_V2_WRITE_DOMAIN_COMPUTED:
    return "computed";
  default:
    return "invalid";
  }
}

static const char *confit_tui_v2_assignment_origin_name(
    ConfitV2AssignmentOrigin origin) {
  switch (origin) {
  case CONFIT_V2_ASSIGNMENT_ORIGIN_SCHEMA_DEFAULT:
    return "schema default";
  case CONFIT_V2_ASSIGNMENT_ORIGIN_PROFILE:
    return "profile";
  case CONFIT_V2_ASSIGNMENT_ORIGIN_TARGET:
    return "target";
  case CONFIT_V2_ASSIGNMENT_ORIGIN_USER:
    return "user";
  case CONFIT_V2_ASSIGNMENT_ORIGIN_UNSET:
    return "unset";
  default:
    return "none";
  }
}

static const char *confit_tui_v2_effective_origin_name(
    ConfitV2EffectiveValueOrigin origin) {
  switch (origin) {
  case CONFIT_V2_EFFECTIVE_VALUE_REQUESTED:
    return "requested";
  case CONFIT_V2_EFFECTIVE_VALUE_CONDITIONAL_DEFAULT:
    return "conditional default";
  case CONFIT_V2_EFFECTIVE_VALUE_DEFAULT:
    return "default";
  case CONFIT_V2_EFFECTIVE_VALUE_COMPUTED:
    return "computed";
  case CONFIT_V2_EFFECTIVE_VALUE_UNSET:
    return "unset";
  default:
    return "none";
  }
}

static void confit_tui_v2_format_value(const ConfitV2Value *value, char *out,
                                        size_t out_size) {
  size_t index;
  size_t used = 0U;

  if (out == 0 || out_size == 0U) {
    return;
  }
  out[0] = '\0';
  if (value == 0 || value->kind == CONFIT_V2_VALUE_UNSET) {
    (void)snprintf(out, out_size, "-");
    return;
  }
  switch (value->kind) {
  case CONFIT_V2_VALUE_BOOL:
    (void)snprintf(out, out_size, "%s", value->as.bool_value ? "true" : "false");
    break;
  case CONFIT_V2_VALUE_TRISTATE:
    (void)snprintf(out, out_size, "%c", value->as.tristate_value);
    break;
  case CONFIT_V2_VALUE_INT:
    (void)snprintf(out, out_size, "%" PRId64, value->as.int_value);
    break;
  case CONFIT_V2_VALUE_UINT:
    (void)snprintf(out, out_size, "%" PRIu64, value->as.uint_value);
    break;
  case CONFIT_V2_VALUE_FLOAT:
    (void)snprintf(out, out_size, "%.17g", value->as.float_value);
    break;
  case CONFIT_V2_VALUE_STRING:
    (void)snprintf(out, out_size, "%s", confit_tui_text_or_dash(value->as.string_value));
    break;
  case CONFIT_V2_VALUE_STRING_LIST:
    for (index = 0U; index < value->as.string_list.count && used + 1U < out_size;
         ++index) {
      const char *item = value->as.string_list.items[index];
      const int written = snprintf(out + used, out_size - used, "%s%s",
                                   index == 0U ? "" : ",", item != 0 ? item : "");
      if (written < 0 || (size_t)written >= out_size - used) {
        used = out_size - 1U;
        break;
      }
      used += (size_t)written;
    }
    break;
  default:
    (void)snprintf(out, out_size, "-");
    break;
  }
  out[out_size - 1U] = '\0';
}

static ConfitStatus confit_tui_v2_append_toml_value(ConfitTuiTextBuilder *builder,
                                                      const ConfitV2Value *value) {
  char number[96];
  size_t index;
  ConfitStatus status;

  if (value == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  switch (value->kind) {
  case CONFIT_V2_VALUE_BOOL:
    return confit_tui_text_append(builder, value->as.bool_value ? "true" : "false");
  case CONFIT_V2_VALUE_TRISTATE:
    number[0] = value->as.tristate_value;
    number[1] = '\0';
    return confit_tui_text_append_quoted(builder, number);
  case CONFIT_V2_VALUE_INT:
    (void)snprintf(number, sizeof(number), "%" PRId64, value->as.int_value);
    return confit_tui_text_append(builder, number);
  case CONFIT_V2_VALUE_UINT:
    (void)snprintf(number, sizeof(number), "%" PRIu64, value->as.uint_value);
    return confit_tui_text_append(builder, number);
  case CONFIT_V2_VALUE_FLOAT:
    (void)snprintf(number, sizeof(number), "%.17g", value->as.float_value);
    return confit_tui_text_append(builder, number);
  case CONFIT_V2_VALUE_STRING:
    return confit_tui_text_append_quoted(builder, value->as.string_value);
  case CONFIT_V2_VALUE_STRING_LIST:
    status = confit_tui_text_append(builder, "[");
    for (index = 0U; status == CONFIT_OK && index < value->as.string_list.count;
         ++index) {
      if (index > 0U) {
        status = confit_tui_text_append(builder, ", ");
      }
      if (status == CONFIT_OK) {
        status = confit_tui_text_append_quoted(builder,
                                                value->as.string_list.items[index]);
      }
    }
    if (status == CONFIT_OK) {
      status = confit_tui_text_append(builder, "]");
    }
    return status;
  default:
    return CONFIT_ERR_SCHEMA;
  }
}

static ConfitStatus confit_tui_v2_append_source_value(
    ConfitTuiTextBuilder *builder, const ConfitV2TomlValue *value) {
  char number[96];
  size_t index;
  ConfitStatus status;
  int bool_value;
  int64_t int_value;
  double float_value;
  const char *string_value;
  size_t string_size;

  if (value == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  switch (confit_v2_toml_value_type(value)) {
  case CONFIT_V2_TOML_VALUE_BOOL:
    if (!confit_v2_toml_value_bool(value, &bool_value)) {
      return CONFIT_ERR_SCHEMA;
    }
    return confit_tui_text_append(builder, bool_value ? "true" : "false");
  case CONFIT_V2_TOML_VALUE_INT64:
    if (!confit_v2_toml_value_int64(value, &int_value)) {
      return CONFIT_ERR_SCHEMA;
    }
    (void)snprintf(number, sizeof(number), "%" PRId64, int_value);
    return confit_tui_text_append(builder, number);
  case CONFIT_V2_TOML_VALUE_FLOAT64:
    if (!confit_v2_toml_value_float64(value, &float_value)) {
      return CONFIT_ERR_SCHEMA;
    }
    (void)snprintf(number, sizeof(number), "%.17g", float_value);
    return confit_tui_text_append(builder, number);
  case CONFIT_V2_TOML_VALUE_STRING:
    if (!confit_v2_toml_value_string(value, &string_value, &string_size) ||
        string_value[string_size] != '\0') {
      return CONFIT_ERR_SCHEMA;
    }
    return confit_tui_text_append_quoted(builder, string_value);
  case CONFIT_V2_TOML_VALUE_ARRAY:
    status = confit_tui_text_append(builder, "[");
    for (index = 0U; status == CONFIT_OK &&
                    index < confit_v2_toml_array_size(value);
         ++index) {
      if (index > 0U) {
        status = confit_tui_text_append(builder, ", ");
      }
      if (status == CONFIT_OK) {
        status = confit_tui_v2_append_source_value(
            builder, confit_v2_toml_array_at(value, index));
      }
    }
    if (status == CONFIT_OK) {
      status = confit_tui_text_append(builder, "]");
    }
    return status;
  default:
    return CONFIT_ERR_SCHEMA;
  }
}

static size_t confit_tui_v2_find_menu(const ConfitTuiV2State *state,
                                      const char *id) {
  size_t index;

  if (id == 0) {
    return 0U;
  }
  for (index = 1U; index < state->menu_count; ++index) {
    if (state->menus[index].source != 0 &&
        strcmp(state->menus[index].source->id, id) == 0) {
      return index;
    }
  }
  return 0U;
}

static void confit_tui_v2_clear_menus(ConfitTuiV2State *state) {
  free(state->menus);
  state->menus = 0;
  state->menu_count = 0U;
  state->current_menu_index = 0U;
}

static ConfitStatus confit_tui_v2_build_menus(ConfitTuiV2State *state) {
  size_t index;

  state->menus = (ConfitTuiV2Menu *)calloc(state->project->menu_count + 1U,
                                            sizeof(state->menus[0]));
  if (state->menus == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  state->menu_count = state->project->menu_count + 1U;
  for (index = 0U; index < state->project->menu_count; ++index) {
    state->menus[index + 1U].source = &state->project->menus[index];
  }
  for (index = 1U; index < state->menu_count; ++index) {
    state->menus[index].parent_index = confit_tui_v2_find_menu(
        state, state->menus[index].source->parent);
  }
  return CONFIT_OK;
}

static const ConfitTuiV2Edit *confit_tui_v2_find_edit(
    const ConfitTuiV2State *state, const char *option_id, size_t *out_index) {
  size_t index;

  for (index = 0U; index < state->edit_count; ++index) {
    if (strcmp(state->edits[index].option_id, option_id) == 0) {
      if (out_index != 0) {
        *out_index = index;
      }
      return &state->edits[index];
    }
  }
  return 0;
}

static void confit_tui_v2_clear_edits(ConfitTuiV2State *state) {
  size_t index;

  for (index = 0U; index < state->edit_count; ++index) {
    free(state->edits[index].option_id);
    free(state->edits[index].value_text);
  }
  free(state->edits);
  state->edits = 0;
  state->edit_count = 0U;
}

static ConfitStatus confit_tui_v2_make_options(
    const ConfitTuiV2State *state, const char *candidate_id,
    const char *candidate_value, ConfitV2LedgerOptions *out_options,
    ConfitV2ProfileOverride **out_overrides) {
  ConfitV2ProfileOverride *overrides;
  size_t count;
  size_t index;
  int candidate_replaces = 0;

  if (state == 0 || out_options == 0 || out_overrides == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_overrides = 0;
  memset(out_options, 0, sizeof(*out_options));
  count = state->edit_count;
  if (candidate_id != 0 && confit_tui_v2_find_edit(state, candidate_id, 0) == 0) {
    count += 1U;
  }
  overrides = 0;
  if (count > 0U) {
    overrides = (ConfitV2ProfileOverride *)calloc(count, sizeof(overrides[0]));
    if (overrides == 0) {
      return CONFIT_ERR_INTERNAL;
    }
  }
  for (index = 0U; index < state->edit_count; ++index) {
    overrides[index].option_id = state->edits[index].option_id;
    overrides[index].value_text = state->edits[index].value_text;
    overrides[index].span.path = kTuiV2TransactionSource;
    overrides[index].span.line = index + 1U;
    if (candidate_id != 0 && strcmp(candidate_id, state->edits[index].option_id) == 0) {
      overrides[index].value_text = candidate_value;
      candidate_replaces = 1;
    }
  }
  if (candidate_id != 0 && !candidate_replaces) {
    overrides[count - 1U].option_id = candidate_id;
    overrides[count - 1U].value_text = candidate_value;
    overrides[count - 1U].span.path = kTuiV2TransactionSource;
    overrides[count - 1U].span.line = count;
  }
  out_options->profile_name = state->options->profile_name;
  out_options->target_name = state->options->target_name;
  out_options->profile_overrides = overrides;
  out_options->profile_override_count = count;
  *out_overrides = overrides;
  return CONFIT_OK;
}

static ConfitStatus confit_tui_v2_validate_snapshot_artifacts(
    const ConfitTuiV2State *state, const ConfitV2Snapshot *snapshot,
    ConfitDiagnostic *diagnostic) {
  ConfitV2ArtifactOptions options;
  ConfitV2ArtifactSet artifacts;
  ConfitStatus status;

  memset(&options, 0, sizeof(options));
  memset(&artifacts, 0, sizeof(artifacts));
  options.compiled = state->compiled;
  status = confit_v2_generate_artifacts(snapshot, &options, &artifacts,
                                         diagnostic);
  confit_v2_artifact_set_clear(&artifacts);
  return status;
}

static ConfitStatus confit_tui_v2_preview(const ConfitTuiV2State *state,
                                           const char *candidate_id,
                                           const char *candidate_value,
                                           ConfitV2Snapshot **out_snapshot,
                                           ConfitDiagnostic *diagnostic) {
  ConfitV2LedgerOptions options;
  ConfitV2ProfileOverride *overrides;
  ConfitV2InvalidationSet *affected;
  ConfitStatus status;

  if (out_snapshot == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_snapshot = 0;
  overrides = 0;
  affected = 0;
  status = confit_tui_v2_make_options(state, candidate_id, candidate_value,
                                      &options, &overrides);
  if (status == CONFIT_OK && state->snapshot != 0 && candidate_id != 0) {
    status = confit_v2_snapshot_reconcile_edit(
        state->snapshot, state->compiled, &options, candidate_id, out_snapshot,
        &affected, diagnostic);
  } else if (status == CONFIT_OK) {
    status = confit_v2_snapshot_resolve(state->compiled, &options, out_snapshot,
                                        diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_v2_validate_snapshot_artifacts(state, *out_snapshot,
                                                        diagnostic);
  }
  if (status != CONFIT_OK) {
    confit_v2_snapshot_free(*out_snapshot);
    *out_snapshot = 0;
  }
  confit_v2_invalidation_set_free(affected);
  free(overrides);
  return status;
}

static void confit_tui_v2_status_from_diagnostic(ConfitTuiV2State *state,
                                                   const char *prefix,
                                                   ConfitStatus status,
                                                   const ConfitDiagnostic *diag) {
  const char *message = diag != 0 && diag->message != 0 ? diag->message
                                                         : confit_status_name(status);

  (void)snprintf(state->status, sizeof(state->status), "%s: %s%s%s", prefix,
                 diag != 0 && diag->path != 0 ? diag->path : "",
                 diag != 0 && diag->path != 0 ? ": " : "", message);
  state->status[sizeof(state->status) - 1U] = '\0';
}

static ConfitStatus confit_tui_v2_refresh_rows(ConfitTuiV2State *state) {
  const ConfitTuiV2Menu *menu;
  size_t index;
  size_t row_count = 0U;

  free(state->rows);
  state->rows = 0;
  state->row_count = 0U;
  state->selected_index = 0U;
  state->rows = (ConfitTuiV2Row *)calloc(state->menu_count + state->project->symbol_count,
                                          sizeof(state->rows[0]));
  if (state->rows == 0 && state->menu_count + state->project->symbol_count > 0U) {
    return CONFIT_ERR_INTERNAL;
  }
  menu = &state->menus[state->current_menu_index];
  for (index = 1U; !state->flat_view && index < state->menu_count; ++index) {
    ConfitTuiV2Row *row;

    if (state->menus[index].parent_index != state->current_menu_index) {
      continue;
    }
    row = &state->rows[row_count++];
    row->kind = CONFIT_TUI_V2_ROW_MENU;
    row->menu_index = index;
    (void)snprintf(row->label, sizeof(row->label), "%s",
                   confit_tui_text_or_dash(state->menus[index].source->prompt));
    (void)snprintf(row->detail, sizeof(row->detail), "menu: %s",
                   state->menus[index].source->id);
    row->item.label = row->label;
    row->item.detail = row->detail;
    row->item.is_heading = 1;
    row->item.expanded = 0;
  }
  for (index = 0U; index < state->project->symbol_count; ++index) {
    const ConfitV2Symbol *symbol = &state->project->symbols[index];
    const ConfitV2SnapshotOption *snapshot_option;
    ConfitTuiV2Row *row;
    char marker[8];
    const char *prompt;

    if ((!state->flat_view && menu->source == 0 && symbol->menu != 0) ||
        (menu->source != 0 &&
         (symbol->menu == 0 || strcmp(symbol->menu, menu->source->id) != 0)) ||
        (state->text_filter[0] != '\0' &&
         !confit_tui_v2_symbol_matches(symbol, state->text_filter))) {
      continue;
    }
    snapshot_option = confit_v2_snapshot_find_option(state->snapshot, symbol->id);
    if (snapshot_option == 0) {
      return CONFIT_ERR_INTERNAL;
    }
    row = &state->rows[row_count++];
    row->kind = CONFIT_TUI_V2_ROW_OPTION;
    row->symbol = symbol;
    prompt = symbol->prompt != 0 ? symbol->prompt : symbol->id;
    marker[0] = '\0';
    if (symbol->type == CONFIT_V2_OPTION_TYPE_BOOL) {
      (void)snprintf(marker, sizeof(marker), "[%c] ",
                     snapshot_option->effective_is_set &&
                             snapshot_option->effective_value.as.bool_value
                         ? '*'
                         : ' ');
    } else if (symbol->type == CONFIT_V2_OPTION_TYPE_TRISTATE) {
      (void)snprintf(marker, sizeof(marker), "[%c] ",
                     snapshot_option->effective_is_set
                         ? snapshot_option->effective_value.as.tristate_value
                         : ' ');
    }
    confit_tui_v2_format_value(snapshot_option->effective_is_set
                                    ? &snapshot_option->effective_value
                                    : 0,
                                row->value, sizeof(row->value));
    (void)snprintf(row->label, sizeof(row->label), "%s%s", marker, prompt);
    if (!snapshot_option->available) {
      (void)snprintf(row->detail, sizeof(row->detail), "unavailable");
    } else if (!snapshot_option->visible) {
      (void)snprintf(row->detail, sizeof(row->detail), "hidden");
    } else if (symbol->write_domain == CONFIT_V2_WRITE_DOMAIN_COMPUTED) {
      (void)snprintf(row->detail, sizeof(row->detail), "computed");
    } else if (symbol->write_domain != CONFIT_V2_WRITE_DOMAIN_PROFILE) {
      (void)snprintf(row->detail, sizeof(row->detail), "read-only %s domain",
                     confit_tui_v2_domain_name(symbol->write_domain));
    } else {
      (void)snprintf(row->detail, sizeof(row->detail), "profile writable");
    }
    row->item.label = row->label;
    row->item.detail = row->detail;
    row->item.value = row->value;
    row->item.is_disabled = !snapshot_option->available || !snapshot_option->visible ||
                            symbol->write_domain != CONFIT_V2_WRITE_DOMAIN_PROFILE;
  }
  state->row_count = row_count;
  return CONFIT_OK;
}

static void confit_tui_v2_breadcrumb(const ConfitTuiV2State *state, char *out,
                                     size_t out_size) {
  const size_t stack_limit = state->menu_count + 1U;
  size_t *stack = (size_t *)calloc(stack_limit, sizeof(stack[0]));
  size_t count = 0U;
  size_t cursor = state->current_menu_index;
  size_t index;

  if (out == 0 || out_size == 0U) {
    free(stack);
    return;
  }
  out[0] = '\0';
  while (count < stack_limit) {
    stack[count++] = cursor;
    if (cursor == 0U) {
      break;
    }
    cursor = state->menus[cursor].parent_index;
  }
  for (index = count; index > 0U; --index) {
    const ConfitTuiV2Menu *menu = &state->menus[stack[index - 1U]];
    const char *name = menu->source == 0 ? "Main Menu" : menu->source->prompt;
    const size_t used = strlen(out);

    (void)snprintf(out + used, out_size - used, "%s%s", used == 0U ? "" : " > ",
                   confit_tui_text_or_dash(name));
  }
  out[out_size - 1U] = '\0';
  free(stack);
}

static void confit_tui_v2_inspector(const ConfitTuiV2State *state,
                                    const ConfitTuiV2Row *row, char *out,
                                    size_t out_size) {
  const ConfitV2SnapshotOption *option;
  char requested[160];
  char effective[160];

  if (out == 0 || out_size == 0U) {
    return;
  }
  if (row == 0) {
    (void)snprintf(out, out_size, "Inspector: no selection");
    return;
  }
  if (row->kind == CONFIT_TUI_V2_ROW_MENU) {
    (void)snprintf(out, out_size, "Inspector: menu %s | Enter opens this menu",
                   state->menus[row->menu_index].source->id);
    return;
  }
  option = confit_v2_snapshot_find_option(state->snapshot, row->symbol->id);
  if (option == 0) {
    (void)snprintf(out, out_size, "Inspector: snapshot lookup failed");
    return;
  }
  confit_tui_v2_format_value(option->requested.is_present && option->requested.is_set
                                  ? &option->requested.value
                                  : 0,
                              requested, sizeof(requested));
  confit_tui_v2_format_value(option->effective_is_set ? &option->effective_value : 0,
                              effective, sizeof(effective));
  if (state->verbose_inspector) {
    (void)snprintf(
        out, out_size,
        "Inspector verbose: id=%s type=%s domain=%s | requested=%s (%s, %s) | effective=%s (%s, %s) | available=%s visible=%s",
        option->id, confit_tui_v2_type_name(option->type),
        confit_tui_v2_domain_name(option->write_domain), requested,
        confit_tui_v2_assignment_origin_name(option->requested.origin),
        confit_tui_text_or_dash(option->requested.source_path), effective,
        confit_tui_v2_effective_origin_name(option->effective_origin),
        confit_tui_text_or_dash(option->effective_source_path),
        option->available ? "yes" : "no", option->visible ? "yes" : "no");
  } else {
    (void)snprintf(out, out_size, "Inspector: %s <%s> %s effective=%s %s%s",
                   confit_tui_text_or_dash(row->symbol->prompt), option->id,
                   confit_tui_v2_type_name(option->type), effective,
                   option->available ? "" : "unavailable ",
                   option->visible ? "" : "hidden");
  }
  out[out_size - 1U] = '\0';
}

static ConfitStatus confit_tui_v2_render(ConfitTuiV2State *state) {
  ConfitTuiListItem *items;
  ConfitTuiScreen screen;
  char header[512];
  char breadcrumb[256];
  char inspector[768];
  size_t index;

  items = 0;
  if (state->row_count > 0U) {
    items = (ConfitTuiListItem *)calloc(state->row_count, sizeof(items[0]));
    if (items == 0) {
      return CONFIT_ERR_INTERNAL;
    }
    for (index = 0U; index < state->row_count; ++index) {
      items[index] = state->rows[index].item;
    }
  }
  confit_tui_v2_breadcrumb(state, breadcrumb, sizeof(breadcrumb));
  (void)snprintf(header, sizeof(header),
                 "mode=profile schema=v2 project=%s profile=%s target=%s dirty=%s view=%s\nbreadcrumb=%s | row %lu/%lu | search=%s %lu/%lu | filter=%s",
                 confit_tui_text_or_dash(state->project->name),
                 confit_tui_text_or_dash(state->options->profile_name),
                 confit_tui_text_or_dash(confit_v2_snapshot_target_name(state->snapshot)),
                 state->dirty ? "yes" : "no", state->flat_view ? "flat" : "tree", breadcrumb,
                 state->row_count == 0U ? 0UL : (unsigned long)(state->selected_index + 1U),
                 (unsigned long)state->row_count, confit_tui_text_or_dash(state->search),
                 (unsigned long)state->search_position,
                 (unsigned long)state->search_count,
                 confit_tui_text_or_dash(state->text_filter));
  header[sizeof(header) - 1U] = '\0';
  confit_tui_v2_inspector(state,
                           state->selected_index < state->row_count
                               ? &state->rows[state->selected_index]
                               : 0,
                           inspector, sizeof(inspector));
  memset(&screen, 0, sizeof(screen));
  screen.title = "Confit TUI - schema v2 profile";
  screen.header = header;
  screen.inspector = inspector;
  screen.key_legend = "keys: move | Enter edit/menu | s save | / search | ? help | v detail | Esc back/exit";
  screen.items = items;
  screen.item_count = state->row_count;
  screen.selected_index = state->selected_index;
  screen.status = state->status[0] != '\0' ? state->status : "ready";
  if (confit_tui_curses_render(&screen) != 0) {
    free(items);
    return CONFIT_ERR_INTERNAL;
  }
  free(items);
  return CONFIT_OK;
}

static int confit_tui_v2_contains_casefold(const char *haystack,
                                            const char *needle) {
  size_t needle_size;
  size_t index;

  if (needle == 0 || needle[0] == '\0') {
    return 1;
  }
  if (haystack == 0) {
    return 0;
  }
  needle_size = strlen(needle);
  for (index = 0U; haystack[index] != '\0'; ++index) {
    size_t cursor;

    for (cursor = 0U; cursor < needle_size && haystack[index + cursor] != '\0';
         ++cursor) {
      if (tolower((unsigned char)haystack[index + cursor]) !=
          tolower((unsigned char)needle[cursor])) {
        break;
      }
    }
    if (cursor == needle_size) {
      return 1;
    }
  }
  return 0;
}

static int confit_tui_v2_symbol_matches(const ConfitV2Symbol *symbol,
                                         const char *query) {
  size_t index;

  if (confit_tui_v2_contains_casefold(symbol->id, query) ||
      confit_tui_v2_contains_casefold(symbol->prompt, query) ||
      confit_tui_v2_contains_casefold(symbol->help, query) ||
      confit_tui_v2_contains_casefold(symbol->menu, query)) {
    return 1;
  }
  for (index = 0U; index < symbol->tags.count; ++index) {
    if (confit_tui_v2_contains_casefold(symbol->tags.items[index], query)) {
      return 1;
    }
  }
  return 0;
}

static ConfitStatus confit_tui_v2_jump_search(ConfitTuiV2State *state,
                                               size_t match_position) {
  const ConfitV2Symbol *symbol;
  size_t index;
  ConfitStatus status;

  if (state->search_count == 0U || match_position >= state->search_count) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  symbol = &state->project->symbols[state->search_matches[match_position]];
  state->flat_view = 0;
  state->current_menu_index = confit_tui_v2_find_menu(state, symbol->menu);
  status = confit_tui_v2_refresh_rows(state);
  if (status != CONFIT_OK) {
    return status;
  }
  for (index = 0U; index < state->row_count; ++index) {
    if (state->rows[index].kind == CONFIT_TUI_V2_ROW_OPTION &&
        state->rows[index].symbol == symbol) {
      state->selected_index = index;
      break;
    }
  }
  state->search_position = match_position + 1U;
  (void)snprintf(state->status, sizeof(state->status), "search %lu/%lu: %s",
                 (unsigned long)state->search_position,
                 (unsigned long)state->search_count, symbol->id);
  state->status[sizeof(state->status) - 1U] = '\0';
  return CONFIT_OK;
}

static ConfitStatus confit_tui_v2_prompt_search(ConfitTuiV2State *state) {
  char query[128];
  size_t index;
  int input_status;
  ConfitStatus status;

  input_status = confit_tui_curses_read_mode_line(CONFIT_TUI_INPUT_SEARCH,
                                                  "search: ", query,
                                                  sizeof(query));
  if (input_status != CONFIT_TUI_INPUT_ACCEPTED) {
    (void)snprintf(state->status, sizeof(state->status), "search cancelled");
    return CONFIT_OK;
  }
  free(state->search_matches);
  state->search_matches = 0;
  state->search_count = 0U;
  state->search_position = 0U;
  (void)snprintf(state->search, sizeof(state->search), "%s", query);
  state->search[sizeof(state->search) - 1U] = '\0';
  if (query[0] == '\0') {
    (void)snprintf(state->status, sizeof(state->status), "empty search");
    return CONFIT_OK;
  }
  state->search_matches = (size_t *)calloc(state->project->symbol_count,
                                            sizeof(state->search_matches[0]));
  if (state->search_matches == 0 && state->project->symbol_count > 0U) {
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < state->project->symbol_count; ++index) {
    if (confit_tui_v2_symbol_matches(&state->project->symbols[index], query)) {
      state->search_matches[state->search_count++] = index;
    }
  }
  if (state->search_count == 0U) {
    (void)snprintf(state->status, sizeof(state->status), "search: no matches for %s",
                   query);
    return CONFIT_OK;
  }
  status = confit_tui_v2_jump_search(state, 0U);
  return status;
}

static ConfitStatus confit_tui_v2_show_help(ConfitTuiV2State *state) {
  const ConfitTuiV2Row *row;
  const ConfitV2SnapshotOption *option;
  char body[2048];
  char current[160];
  char requested[160];
  ConfitTuiKey key;
  size_t first_line = 0U;

  if (state->selected_index >= state->row_count) {
    return CONFIT_OK;
  }
  row = &state->rows[state->selected_index];
  if (row->kind == CONFIT_TUI_V2_ROW_MENU) {
    (void)snprintf(body, sizeof(body), "menu: %s\nprompt: %s\n\nEsc returns to browse.",
                   state->menus[row->menu_index].source->id,
                   confit_tui_text_or_dash(state->menus[row->menu_index].source->prompt));
  } else {
    option = confit_v2_snapshot_find_option(state->snapshot, row->symbol->id);
    if (option == 0) {
      return CONFIT_ERR_INTERNAL;
    }
    confit_tui_v2_format_value(option->effective_is_set ? &option->effective_value : 0,
                                current, sizeof(current));
    confit_tui_v2_format_value(option->requested.is_present && option->requested.is_set
                                    ? &option->requested.value
                                    : 0,
                                requested, sizeof(requested));
    (void)snprintf(
        body, sizeof(body),
        "prompt: %s\nid: %s\ntype: %s\nwrite domain: %s\ncurrent effective: %s (%s)\nrequested: %s (%s)\nrequested source: %s\neffective source: %s\navailable: %s\nvisible: %s\nmenu: %s\nhelp:\n%s",
        confit_tui_text_or_dash(row->symbol->prompt), option->id,
        confit_tui_v2_type_name(option->type),
        confit_tui_v2_domain_name(option->write_domain), current,
        confit_tui_v2_effective_origin_name(option->effective_origin), requested,
        confit_tui_v2_assignment_origin_name(option->requested.origin),
        confit_tui_text_or_dash(option->requested.source_path),
        confit_tui_text_or_dash(option->effective_source_path),
        option->available ? "yes" : "no", option->visible ? "yes" : "no",
        confit_tui_text_or_dash(row->symbol->menu),
        confit_tui_text_or_dash(row->symbol->help));
  }
  body[sizeof(body) - 1U] = '\0';
  do {
    if (confit_tui_curses_render_text("Confit V2 Detail", "schema v2 immutable snapshot",
                                      body, "keys: PageUp/PageDown | Esc back",
                                      "detail", first_line) != 0) {
      return CONFIT_ERR_INTERNAL;
    }
    key = confit_tui_curses_read_key();
    if (key == CONFIT_TUI_KEY_PAGE_DOWN) {
      first_line += confit_tui_curses_page_step();
    } else if (key == CONFIT_TUI_KEY_PAGE_UP && first_line > 0U) {
      const size_t step = confit_tui_curses_page_step();
      first_line = first_line > step ? first_line - step : 0U;
    }
  } while (key != CONFIT_TUI_KEY_CANCEL && key != CONFIT_TUI_KEY_QUIT);
  (void)snprintf(state->status, sizeof(state->status), "detail closed");
  return CONFIT_OK;
}

static ConfitStatus confit_tui_v2_apply_edit(ConfitTuiV2State *state,
                                              const ConfitV2Symbol *symbol,
                                              const char *value_text,
                                              ConfitDiagnostic *diagnostic) {
  ConfitV2Snapshot *preview;
  ConfitTuiV2Edit *grown;
  size_t edit_index;
  const ConfitTuiV2Edit *existing;
  char *new_text;
  ConfitStatus status;

  preview = 0;
  status = confit_tui_v2_preview(state, symbol->id, value_text, &preview,
                                  diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  new_text = confit_tui_v2_strdup(value_text);
  if (new_text == 0) {
    confit_v2_snapshot_free(preview);
    return CONFIT_ERR_INTERNAL;
  }
  existing = confit_tui_v2_find_edit(state, symbol->id, &edit_index);
  if (existing != 0) {
    free(state->edits[edit_index].value_text);
    state->edits[edit_index].value_text = new_text;
  } else {
    grown = (ConfitTuiV2Edit *)realloc(state->edits,
                                       (state->edit_count + 1U) * sizeof(*grown));
    if (grown == 0) {
      free(new_text);
      confit_v2_snapshot_free(preview);
      return CONFIT_ERR_INTERNAL;
    }
    state->edits = grown;
    state->edits[state->edit_count].option_id = confit_tui_v2_strdup(symbol->id);
    state->edits[state->edit_count].value_text = new_text;
    if (state->edits[state->edit_count].option_id == 0) {
      free(new_text);
      state->edits[state->edit_count].value_text = 0;
      confit_v2_snapshot_free(preview);
      return CONFIT_ERR_INTERNAL;
    }
    state->edit_count += 1U;
  }
  confit_v2_snapshot_free(state->snapshot);
  state->snapshot = preview;
  state->dirty = 1;
  status = confit_tui_v2_refresh_rows(state);
  if (status == CONFIT_OK) {
    (void)snprintf(state->status, sizeof(state->status), "preview accepted: %s = %s",
                   symbol->id, value_text);
    state->status[sizeof(state->status) - 1U] = '\0';
  }
  return status;
}

static int confit_tui_v2_dialog_validator(const char *text, char *message,
                                           size_t message_size, void *user) {
  ConfitTuiV2Validator *validator = (ConfitTuiV2Validator *)user;
  ConfitV2Snapshot *preview;
  ConfitDiagnostic diagnostic;
  ConfitStatus status;

  if (validator == 0 || validator->state == 0 || validator->symbol == 0) {
    (void)snprintf(message, message_size, "invalid v2 value validator");
    return 1;
  }
  confit_diagnostic_init(&diagnostic);
  preview = 0;
  status = confit_tui_v2_preview(validator->state, validator->symbol->id, text,
                                  &preview, &diagnostic);
  confit_v2_snapshot_free(preview);
  if (status == CONFIT_OK) {
    return 0;
  }
  (void)snprintf(message, message_size, "%s",
                 diagnostic.message != 0 ? diagnostic.message : confit_status_name(status));
  message[message_size - 1U] = '\0';
  return 1;
}

static ConfitStatus confit_tui_v2_prompt_enum(ConfitTuiV2State *state,
                                               const ConfitV2Symbol *symbol,
                                               ConfitDiagnostic *diagnostic) {
  const ConfitV2SnapshotOption *option;
  const char **choices;
  size_t selected = 0U;
  size_t index;
  int input_status;
  char header[640];
  ConfitStatus status;

  option = confit_v2_snapshot_find_option(state->snapshot, symbol->id);
  if (option == 0 || symbol->values.count == 0U) {
    return CONFIT_ERR_SCHEMA;
  }
  choices = (const char **)calloc(symbol->values.count, sizeof(choices[0]));
  if (choices == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  for (index = 0U; index < symbol->values.count; ++index) {
    choices[index] = symbol->values.items[index];
    if (option->effective_is_set && option->effective_value.kind == CONFIT_V2_VALUE_STRING &&
        strcmp(option->effective_value.as.string_value, choices[index]) == 0) {
      selected = index;
    }
  }
  (void)snprintf(header, sizeof(header),
                 "option: %s\nprompt: %s\ntype: enum\nwrite domain: profile\nkeys: j/k move, Enter selects, Esc cancels",
                 symbol->id, confit_tui_text_or_dash(symbol->prompt));
  input_status = confit_tui_curses_select_dialog("Confit V2 Choice", header,
                                                   choices, symbol->values.count,
                                                   selected, &selected);
  if (input_status == CONFIT_TUI_INPUT_ACCEPTED) {
    status = confit_tui_v2_apply_edit(state, symbol, choices[selected], diagnostic);
  } else {
    (void)snprintf(state->status, sizeof(state->status), "dialog cancelled");
    status = CONFIT_OK;
  }
  free(choices);
  return status;
}

static ConfitStatus confit_tui_v2_prompt_tristate(ConfitTuiV2State *state,
                                                   const ConfitV2Symbol *symbol,
                                                   ConfitDiagnostic *diagnostic) {
  static const char *choices[] = {"n", "m", "y"};
  const ConfitV2SnapshotOption *option;
  size_t selected = 0U;
  int input_status;
  char header[512];

  option = confit_v2_snapshot_find_option(state->snapshot, symbol->id);
  if (option != 0 && option->effective_is_set &&
      option->effective_value.kind == CONFIT_V2_VALUE_TRISTATE) {
    if (option->effective_value.as.tristate_value == 'm') {
      selected = 1U;
    } else if (option->effective_value.as.tristate_value == 'y') {
      selected = 2U;
    }
  }
  (void)snprintf(header, sizeof(header),
                 "option: %s\nprompt: %s\ntype: tristate\nkeys: j/k move, Enter selects, Esc cancels",
                 symbol->id, confit_tui_text_or_dash(symbol->prompt));
  input_status = confit_tui_curses_select_dialog("Confit V2 Tristate", header,
                                                   choices, 3U, selected, &selected);
  if (input_status != CONFIT_TUI_INPUT_ACCEPTED) {
    (void)snprintf(state->status, sizeof(state->status), "dialog cancelled");
    return CONFIT_OK;
  }
  return confit_tui_v2_apply_edit(state, symbol, choices[selected], diagnostic);
}

static ConfitStatus confit_tui_v2_prompt_typed(ConfitTuiV2State *state,
                                                const ConfitV2Symbol *symbol,
                                                ConfitDiagnostic *diagnostic) {
  const ConfitV2SnapshotOption *option;
  ConfitTuiV2Validator validator;
  char initial[256];
  char header[768];
  char input[256];
  int input_status;

  option = confit_v2_snapshot_find_option(state->snapshot, symbol->id);
  if (option == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  confit_tui_v2_format_value(option->effective_is_set ? &option->effective_value : 0,
                              initial, sizeof(initial));
  (void)snprintf(header, sizeof(header),
                 "option: %s\nprompt: %s\ntype: %s\ncurrent: %s\nwrite domain: profile\nEnter validates the full v2 transaction. Esc cancels.",
                 symbol->id, confit_tui_text_or_dash(symbol->prompt),
                 confit_tui_v2_type_name(symbol->type), initial);
  validator.state = state;
  validator.symbol = symbol;
  input_status = confit_tui_curses_read_value_dialog(
      "Confit V2 Value", header, "value: ", "type and full constraint validation",
      confit_tui_v2_dialog_validator, &validator, input, sizeof(input));
  if (input_status != CONFIT_TUI_INPUT_ACCEPTED) {
    (void)snprintf(state->status, sizeof(state->status), "dialog cancelled");
    return CONFIT_OK;
  }
  return confit_tui_v2_apply_edit(state, symbol, input, diagnostic);
}

static ConfitStatus confit_tui_v2_prompt_edit(ConfitTuiV2State *state,
                                               const ConfitV2Symbol *symbol,
                                               ConfitDiagnostic *diagnostic) {
  const ConfitV2SnapshotOption *option;
  const char *next;

  option = confit_v2_snapshot_find_option(state->snapshot, symbol->id);
  if (option == 0) {
    return CONFIT_ERR_INTERNAL;
  }
  if (!option->available || !option->visible ||
      symbol->write_domain != CONFIT_V2_WRITE_DOMAIN_PROFILE) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, symbol->id, 0, 0,
                          "option is unavailable, hidden, computed, or outside profile write domain");
    return CONFIT_ERR_SCHEMA;
  }
  if (symbol->type == CONFIT_V2_OPTION_TYPE_BOOL) {
    next = option->effective_is_set && option->effective_value.kind == CONFIT_V2_VALUE_BOOL &&
                   option->effective_value.as.bool_value
               ? "false"
               : "true";
    return confit_tui_v2_apply_edit(state, symbol, next, diagnostic);
  }
  if (symbol->type == CONFIT_V2_OPTION_TYPE_ENUM) {
    return confit_tui_v2_prompt_enum(state, symbol, diagnostic);
  }
  if (symbol->type == CONFIT_V2_OPTION_TYPE_TRISTATE) {
    return confit_tui_v2_prompt_tristate(state, symbol, diagnostic);
  }
  return confit_tui_v2_prompt_typed(state, symbol, diagnostic);
}

static int confit_tui_v2_profile_name_matches(const ConfitV2TomlValue *root,
                                               const char *profile_name) {
  const ConfitV2TomlValue *profile;
  const ConfitV2TomlValue *name;
  const char *text;
  size_t size;

  profile = confit_v2_toml_table_find(root, "profile");
  name = confit_v2_toml_table_find(profile, "name");
  return confit_v2_toml_value_string(name, &text, &size) &&
         strlen(profile_name) == size && strncmp(text, profile_name, size) == 0;
}

static ConfitStatus confit_tui_v2_load_profile_document(
    const ConfitTuiV2State *state, char *out_path, size_t out_path_size,
    ConfitV2TomlDocument **out_document, ConfitDiagnostic *diagnostic) {
  size_t directory_index;

  *out_document = 0;
  for (directory_index = 0U; directory_index < state->project->profile_dirs.count;
       ++directory_index) {
    char directory[1024];
    char **paths;
    size_t path_count;
    size_t path_index;
    ConfitStatus status;

    status = confit_host_path_join(directory, sizeof(directory),
                                   state->project->config_root,
                                   state->project->profile_dirs.items[directory_index],
                                   diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    paths = 0;
    path_count = 0U;
    status = confit_host_list_toml_files(directory, &paths, &path_count, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    for (path_index = 0U; path_index < path_count; ++path_index) {
      ConfitV2TomlDocument *document = 0;

      status = confit_v2_toml_parse_file(paths[path_index], &document, diagnostic);
      if (status != CONFIT_OK) {
        confit_host_string_list_free(paths, path_count);
        return status;
      }
      if (confit_tui_v2_profile_name_matches(confit_v2_toml_document_root(document),
                                              state->options->profile_name)) {
        if (strlen(paths[path_index]) + 1U > out_path_size) {
          confit_v2_toml_document_free(document);
          confit_host_string_list_free(paths, path_count);
          return CONFIT_ERR_INVALID_ARGUMENT;
        }
        memcpy(out_path, paths[path_index], strlen(paths[path_index]) + 1U);
        confit_host_string_list_free(paths, path_count);
        *out_document = document;
        return CONFIT_OK;
      }
      confit_v2_toml_document_free(document);
    }
    confit_host_string_list_free(paths, path_count);
  }
  confit_diagnostic_set(diagnostic, CONFIT_ERR_SCHEMA, state->options->profile_name,
                        0, 0, "cannot save unknown schema v2 profile");
  return CONFIT_ERR_SCHEMA;
}

static int confit_tui_v2_edit_contains(const ConfitTuiV2State *state,
                                        const char *option_id) {
  return confit_tui_v2_find_edit(state, option_id, 0) != 0;
}

static ConfitStatus confit_tui_v2_append_profile_assignment(
    ConfitTuiTextBuilder *builder, const char *id, const ConfitV2TomlValue *source,
    const ConfitTuiV2State *state, ConfitDiagnostic *diagnostic) {
  const ConfitTuiV2Edit *edit;
  const ConfitV2SnapshotOption *option;
  ConfitStatus status;

  status = confit_tui_text_append_quoted(builder, id);
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(builder, " = ");
  }
  edit = confit_tui_v2_find_edit(state, id, 0);
  if (status == CONFIT_OK && edit != 0) {
    option = confit_v2_snapshot_find_option(state->snapshot, id);
    if (option == 0 || !option->requested.is_present || !option->requested.is_set) {
      confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, id, 0, 0,
                            "transaction preview lost profile assignment");
      return CONFIT_ERR_INTERNAL;
    }
    status = confit_tui_v2_append_toml_value(builder, &option->requested.value);
  } else if (status == CONFIT_OK) {
    status = confit_tui_v2_append_source_value(builder, source);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(builder, "\n");
  }
  return status;
}

static ConfitStatus confit_tui_v2_build_profile_toml(
    const ConfitTuiV2State *state, const ConfitV2TomlDocument *document,
    char **out_text, ConfitDiagnostic *diagnostic) {
  const ConfitV2TomlValue *root = confit_v2_toml_document_root(document);
  const ConfitV2TomlValue *profile = confit_v2_toml_table_find(root, "profile");
  const ConfitV2TomlValue *values = confit_v2_toml_table_find(root, "values");
  const ConfitV2TomlValue *unset = confit_v2_toml_table_find(root, "unset");
  const ConfitV2TomlValue *name = confit_v2_toml_table_find(profile, "name");
  const ConfitV2TomlValue *base = confit_v2_toml_table_find(profile, "base");
  const ConfitV2TomlValue *target = confit_v2_toml_table_find(profile, "target");
  const ConfitV2TomlValue *unset_options = confit_v2_toml_table_find(unset, "options");
  const char *name_text;
  const char *base_text;
  const char *target_text;
  size_t text_size;
  ConfitTuiTextBuilder builder;
  ConfitStatus status;
  size_t index;

  *out_text = 0;
  if (!confit_v2_toml_value_string(name, &name_text, &text_size) ||
      strlen(state->options->profile_name) != text_size ||
      strncmp(name_text, state->options->profile_name, text_size) != 0) {
    return CONFIT_ERR_SCHEMA;
  }
  confit_tui_text_builder_init(&builder);
  status = confit_tui_text_append(&builder, "[profile]\nname = ");
  if (status == CONFIT_OK) {
    status = confit_tui_text_append_quoted(&builder, name_text);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(&builder, "\nschema_version = 2\n");
  }
  if (status == CONFIT_OK && confit_v2_toml_value_string(base, &base_text, &text_size)) {
    status = confit_tui_text_append(&builder, "base = ");
    if (status == CONFIT_OK) status = confit_tui_text_append_quoted(&builder, base_text);
    if (status == CONFIT_OK) status = confit_tui_text_append(&builder, "\n");
  }
  if (status == CONFIT_OK &&
      confit_v2_toml_value_string(target, &target_text, &text_size)) {
    status = confit_tui_text_append(&builder, "target = ");
    if (status == CONFIT_OK) status = confit_tui_text_append_quoted(&builder, target_text);
    if (status == CONFIT_OK) status = confit_tui_text_append(&builder, "\n");
  }
  if (status == CONFIT_OK) {
    status = confit_tui_text_append(&builder, "\n[values]\n");
  }
  if (status == CONFIT_OK &&
      confit_v2_toml_value_type(values) == CONFIT_V2_TOML_VALUE_TABLE) {
    for (index = 0U; status == CONFIT_OK && index < confit_v2_toml_table_size(values);
         ++index) {
      status = confit_tui_v2_append_profile_assignment(
          &builder, confit_v2_toml_table_key_at(values, index),
          confit_v2_toml_table_value_at(values, index), state, diagnostic);
    }
  }
  for (index = 0U; status == CONFIT_OK && index < state->edit_count; ++index) {
    const ConfitV2TomlValue *existing =
        confit_v2_toml_table_find(values, state->edits[index].option_id);
    if (existing == 0) {
      status = confit_tui_v2_append_profile_assignment(
          &builder, state->edits[index].option_id, 0, state, diagnostic);
    }
  }
  if (status == CONFIT_OK &&
      confit_v2_toml_value_type(unset_options) == CONFIT_V2_TOML_VALUE_ARRAY) {
    size_t kept = 0U;

    for (index = 0U; index < confit_v2_toml_array_size(unset_options); ++index) {
      const ConfitV2TomlValue *item = confit_v2_toml_array_at(unset_options, index);
      const char *id;

      if (!confit_v2_toml_value_string(item, &id, &text_size) ||
          confit_tui_v2_edit_contains(state, id)) {
        continue;
      }
      if (kept == 0U) {
        status = confit_tui_text_append(&builder, "\n[unset]\noptions = [");
      } else if (status == CONFIT_OK) {
        status = confit_tui_text_append(&builder, ", ");
      }
      if (status == CONFIT_OK) {
        status = confit_tui_text_append_quoted(&builder, id);
      }
      kept += 1U;
    }
    if (status == CONFIT_OK && kept > 0U) {
      status = confit_tui_text_append(&builder, "]\n");
    }
  }
  if (status != CONFIT_OK) {
    free(builder.text);
    return status;
  }
  *out_text = builder.text;
  return CONFIT_OK;
}

static ConfitStatus confit_tui_v2_save(ConfitTuiV2State *state,
                                        ConfitDiagnostic *diagnostic) {
  ConfitV2TomlDocument *document;
  ConfitV2TomlDocument *candidate;
  char path[1024];
  char *toml;
  ConfitV2Snapshot *full_snapshot;
  int changed;
  ConfitStatus status;

  document = 0;
  candidate = 0;
  toml = 0;
  full_snapshot = 0;
  changed = 0;
  status = confit_tui_v2_preview(state, 0, 0, &full_snapshot, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_tui_v2_load_profile_document(state, path, sizeof(path),
                                                 &document, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_v2_build_profile_toml(state, document, &toml, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_toml_parse_text(path, toml, strlen(toml), &candidate,
                                        diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_host_write_text_file_if_changed_atomic(path, toml, &changed,
                                                            diagnostic);
  }
  confit_v2_toml_document_free(candidate);
  confit_v2_toml_document_free(document);
  free(toml);
  if (status == CONFIT_OK) {
    confit_v2_snapshot_free(state->snapshot);
    state->snapshot = full_snapshot;
    full_snapshot = 0;
    confit_tui_v2_clear_edits(state);
    state->dirty = 0;
    (void)snprintf(state->status, sizeof(state->status),
                   "saved schema v2 profile atomically (%s): %s",
                   changed ? "changed" : "unchanged", path);
    state->status[sizeof(state->status) - 1U] = '\0';
    status = confit_tui_v2_refresh_rows(state);
  }
  confit_v2_snapshot_free(full_snapshot);
  return status;
}

static ConfitStatus confit_tui_v2_request_exit(ConfitTuiV2State *state,
                                                int *out_quit,
                                                ConfitDiagnostic *diagnostic) {
  static const char *items[] = {"Save profile", "Discard changes", "Cancel"};
  size_t selected = 0U;
  int result;
  ConfitStatus status;

  *out_quit = 0;
  if (!state->dirty) {
    *out_quit = 1;
    return CONFIT_OK;
  }
  result = confit_tui_curses_select_dialog(
      "Unsaved Schema V2 Profile Changes",
      "The source TOML stays unchanged until full resolve, constraint, artifact, and TOML validation succeed.",
      items, 3U, selected, &selected);
  if (result != CONFIT_TUI_INPUT_ACCEPTED || selected == 2U) {
    (void)snprintf(state->status, sizeof(state->status), "quit cancelled");
    return CONFIT_OK;
  }
  if (selected == 1U) {
    *out_quit = 1;
    return CONFIT_OK;
  }
  status = confit_tui_v2_save(state, diagnostic);
  if (status == CONFIT_OK && !state->dirty) {
    *out_quit = 1;
  }
  return status;
}

static ConfitStatus confit_tui_v2_command(ConfitTuiV2State *state,
                                           ConfitDiagnostic *diagnostic) {
  char command[128];
  int result;

  result = confit_tui_curses_read_command(":", command, sizeof(command));
  if (result != CONFIT_TUI_INPUT_ACCEPTED) {
    (void)snprintf(state->status, sizeof(state->status), "command cancelled");
    return CONFIT_OK;
  }
  if (strcmp(command, "verbose") == 0) {
    state->verbose_inspector = 1;
    (void)snprintf(state->status, sizeof(state->status), "verbose inspector mode");
  } else if (strcmp(command, "noverbose") == 0) {
    state->verbose_inspector = 0;
    (void)snprintf(state->status, sizeof(state->status), "compact inspector mode");
  } else if (strcmp(command, "help") == 0) {
    (void)snprintf(state->status, sizeof(state->status),
                   "commands: verbose noverbose tree flat filter <text> clear help quit");
  } else if (strcmp(command, "tree") == 0) {
    state->flat_view = 0;
    state->current_menu_index = 0U;
    if (confit_tui_v2_refresh_rows(state) != CONFIT_OK) {
      return CONFIT_ERR_INTERNAL;
    }
    (void)snprintf(state->status, sizeof(state->status), "tree view");
  } else if (strcmp(command, "flat") == 0) {
    state->flat_view = 1;
    state->current_menu_index = 0U;
    if (confit_tui_v2_refresh_rows(state) != CONFIT_OK) {
      return CONFIT_ERR_INTERNAL;
    }
    (void)snprintf(state->status, sizeof(state->status), "flat view");
  } else if (strncmp(command, "filter ", 7U) == 0 && command[7] != '\0') {
    (void)snprintf(state->text_filter, sizeof(state->text_filter), "%s", command + 7U);
    state->text_filter[sizeof(state->text_filter) - 1U] = '\0';
    if (confit_tui_v2_refresh_rows(state) != CONFIT_OK) {
      return CONFIT_ERR_INTERNAL;
    }
    (void)snprintf(state->status, sizeof(state->status), "filter: %s", state->text_filter);
  } else if (strcmp(command, "filter") == 0) {
    (void)snprintf(state->status, sizeof(state->status), "usage: :filter <text>");
  } else if (strcmp(command, "clear") == 0) {
    state->text_filter[0] = '\0';
    state->search[0] = '\0';
    state->search_position = 0U;
    state->search_count = 0U;
    free(state->search_matches);
    state->search_matches = 0;
    if (confit_tui_v2_refresh_rows(state) != CONFIT_OK) {
      return CONFIT_ERR_INTERNAL;
    }
    (void)snprintf(state->status, sizeof(state->status), "search and filter cleared");
  } else if (strcmp(command, "quit") == 0) {
    int quit = 0;
    ConfitStatus status = confit_tui_v2_request_exit(state, &quit, diagnostic);

    if (status == CONFIT_OK && quit) {
      state->quit_requested = 1;
      (void)snprintf(state->status, sizeof(state->status), "quit requested");
      return CONFIT_OK;
    }
    return status;
  } else {
    (void)snprintf(state->status, sizeof(state->status), "unknown command: %s", command);
  }
  state->status[sizeof(state->status) - 1U] = '\0';
  return CONFIT_OK;
}

static int confit_tui_v2_move_selection(ConfitTuiV2State *state,
                                         ConfitTuiKey key) {
  size_t step;

  if (key == CONFIT_TUI_KEY_DOWN && state->selected_index + 1U < state->row_count) {
    state->selected_index += 1U;
    return 1;
  }
  if (key == CONFIT_TUI_KEY_UP && state->selected_index > 0U) {
    state->selected_index -= 1U;
    return 1;
  }
  step = confit_tui_curses_page_step();
  if (key == CONFIT_TUI_KEY_PAGE_DOWN && state->row_count > 0U) {
    state->selected_index = state->selected_index + step < state->row_count
                                ? state->selected_index + step
                                : state->row_count - 1U;
    return 1;
  }
  if (key == CONFIT_TUI_KEY_PAGE_UP) {
    state->selected_index = state->selected_index > step ? state->selected_index - step : 0U;
    return 1;
  }
  if (key == CONFIT_TUI_KEY_HOME) {
    state->selected_index = 0U;
    return 1;
  }
  if (key == CONFIT_TUI_KEY_END && state->row_count > 0U) {
    state->selected_index = state->row_count - 1U;
    return 1;
  }
  return 0;
}

static ConfitStatus confit_tui_v2_run_loop(ConfitTuiV2State *state,
                                            ConfitDiagnostic *diagnostic) {
  for (;;) {
    ConfitTuiKey key;
    ConfitStatus status;

    status = confit_tui_v2_render(state);
    if (status != CONFIT_OK) {
      return status;
    }
    key = confit_tui_curses_read_key();
    if (confit_tui_v2_move_selection(state, key)) {
      continue;
    }
    if ((key == CONFIT_TUI_KEY_CANCEL || key == CONFIT_TUI_KEY_LEFT) &&
        state->current_menu_index != 0U) {
      state->current_menu_index = state->menus[state->current_menu_index].parent_index;
      status = confit_tui_v2_refresh_rows(state);
      if (status != CONFIT_OK) {
        return status;
      }
      (void)snprintf(state->status, sizeof(state->status), "returned to parent menu");
      continue;
    }
    if (key == CONFIT_TUI_KEY_QUIT ||
        (key == CONFIT_TUI_KEY_CANCEL && state->current_menu_index == 0U)) {
      int quit = 0;

      status = confit_tui_v2_request_exit(state, &quit, diagnostic);
      if (status != CONFIT_OK) {
        return status;
      }
      if (quit) {
        return CONFIT_OK;
      }
      continue;
    }
    if (key == CONFIT_TUI_KEY_SEARCH) {
      status = confit_tui_v2_prompt_search(state);
    } else if ((key == CONFIT_TUI_KEY_SEARCH_PREVIOUS || key == CONFIT_TUI_KEY_NEW) &&
               state->search_count > 0U) {
      size_t position = state->search_position == 0U ? 0U : state->search_position - 1U;

      if (key == CONFIT_TUI_KEY_SEARCH_PREVIOUS) {
        position = position == 0U ? state->search_count - 1U : position - 1U;
      } else {
        position = (position + 1U) % state->search_count;
      }
      status = confit_tui_v2_jump_search(state, position);
    } else if (key == CONFIT_TUI_KEY_HELP || key == CONFIT_TUI_KEY_KEYMAP_HELP) {
      status = confit_tui_v2_show_help(state);
    } else if (key == CONFIT_TUI_KEY_VERBOSE_INSPECTOR) {
      state->verbose_inspector = !state->verbose_inspector;
      (void)snprintf(state->status, sizeof(state->status), "%s inspector mode",
                     state->verbose_inspector ? "verbose" : "compact");
      status = CONFIT_OK;
    } else if (key == CONFIT_TUI_KEY_COMMAND) {
      status = confit_tui_v2_command(state, diagnostic);
    } else if (key == CONFIT_TUI_KEY_SAVE) {
      status = confit_tui_v2_save(state, diagnostic);
    } else if ((key == CONFIT_TUI_KEY_ENTER || key == CONFIT_TUI_KEY_EDIT) &&
               state->selected_index < state->row_count) {
      ConfitTuiV2Row *row = &state->rows[state->selected_index];

      if (row->kind == CONFIT_TUI_V2_ROW_MENU) {
        const size_t menu_index = row->menu_index;
        const char *menu_id = state->menus[menu_index].source->id;

        state->current_menu_index = menu_index;
        status = confit_tui_v2_refresh_rows(state);
        if (status == CONFIT_OK) {
          (void)snprintf(state->status, sizeof(state->status), "entered menu %s",
                         menu_id);
        }
      } else {
        status = confit_tui_v2_prompt_edit(state, row->symbol, diagnostic);
      }
    } else {
      status = CONFIT_OK;
    }
    if (status != CONFIT_OK) {
      confit_tui_v2_status_from_diagnostic(state, "error", status, diagnostic);
    }
    if (state->quit_requested) {
      return CONFIT_OK;
    }
  }
}

static void confit_tui_v2_state_clear(ConfitTuiV2State *state) {
  if (state == 0) {
    return;
  }
  free(state->rows);
  free(state->search_matches);
  confit_tui_v2_clear_edits(state);
  confit_tui_v2_clear_menus(state);
  confit_v2_snapshot_free(state->snapshot);
  confit_v2_compiled_structure_free(state->compiled);
  confit_v2_linked_project_free(state->linked);
  confit_v2_project_free(state->project);
}

ConfitStatus confit_tui_run_profile_editor_v2(const ConfitTuiOptions *options,
                                              ConfitDiagnostic *diagnostic) {
  ConfitTuiV2State state;
  ConfitStatus status;

  if (options == 0 || options->project_root == 0 || options->profile_name == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  memset(&state, 0, sizeof(state));
  state.options = options;
  status = confit_v2_schema_load_project(options->project_root, &state.project,
                                         diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_schema_link_project(state.project, &state.linked, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_compile_structure(state.linked, &state.compiled, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_v2_preview(&state, 0, 0, &state.snapshot, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_v2_build_menus(&state);
  }
  if (status == CONFIT_OK) {
    status = confit_tui_v2_refresh_rows(&state);
  }
  if (status == CONFIT_OK) {
    (void)snprintf(state.status, sizeof(state.status), "schema v2 profile transaction ready");
    status = confit_tui_v2_run_loop(&state, diagnostic);
  }
  confit_tui_v2_state_clear(&state);
  confit_tui_curses_stop();
  return status;
}

ConfitStatus confit_tui_run_schema_editor_v2(const ConfitTuiOptions *options,
                                             ConfitDiagnostic *diagnostic) {
  ConfitV2Project *project;
  char body[1024];
  ConfitTuiKey key;
  ConfitStatus status;

  if (options == 0 || options->project_root == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  project = 0;
  status = confit_v2_schema_load_project(options->project_root, &project,
                                         diagnostic);
  if (status != CONFIT_OK) {
    return status;
  }
  (void)snprintf(body, sizeof(body),
                 "SCHEMA EDIT MODE - guarded\n\nproject: %s\nschema_version: 2\noptions: %lu\nmenus: %lu\n\nV2 profile editing is transactional. Schema source edits remain guarded and are not persisted by this frontend. Use a reviewed TOML edit followed by `confit check` for schema changes.\n\nEsc returns.",
                 confit_tui_text_or_dash(project->name),
                 (unsigned long)project->symbol_count,
                 (unsigned long)project->menu_count);
  do {
    if (confit_tui_curses_render_text("Confit TUI - schema v2 editor", "SCHEMA EDIT MODE - guarded",
                                      body, "keys: Esc back", "guarded schema editor",
                                      0U) != 0) {
      status = CONFIT_ERR_INTERNAL;
      break;
    }
    key = confit_tui_curses_read_key();
    status = CONFIT_OK;
  } while (key != CONFIT_TUI_KEY_CANCEL && key != CONFIT_TUI_KEY_QUIT);
  confit_v2_project_free(project);
  confit_tui_curses_stop();
  return status;
}
