#include "confit/ui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/limits.h"
#include "confit/resolver.h"

#define CONFIT_UI_COMMAND_LIMIT ((size_t)64U)

typedef struct ConfitUiRow {
  ConfitUiRowKind kind;
  size_t index;
} ConfitUiRow;

typedef struct ConfitUiHistoryEntry {
  size_t config_index;
  ConfitValue before;
  ConfitValue after;
} ConfitUiHistoryEntry;

struct ConfitUiModel {
  ConfitAllocator allocator;
  const ConfitCatalog *catalog;
  const ConfitDependencyPlan *plan;
  ConfitResolution *resolution;
  ConfitValue *working;
  ConfitValue *saved;
  ConfitValue *pending_saved;
  size_t value_count;
  ConfitUiRow *rows;
  size_t row_count;
  size_t row_capacity;
  size_t current_menu;
  size_t cursor;
  size_t viewport_offset;
  size_t viewport_rows;
  ConfitUiState state;
  int show_unavailable;
  char input[CONFIT_LIMIT_STRING_BYTES + 1U];
  size_t input_size;
  size_t enum_selection;
  ConfitUiHistoryEntry history[CONFIT_UI_HISTORY_LIMIT];
  size_t history_count;
  size_t history_position;
  int save_pending;
  int exit_after_save;
  const char *notice;
};

static const char kInvalidArgument[] = "invalid UI model argument";
static const char kInvalidAllocator[] = "allocator capability is incomplete";
static const char kOutOfMemory[] = "failed to allocate UI model";
static const char kInvalidAction[] =
    "action is not valid in the current UI state";
static const char kNoSelection[] = "no configurable row is selected";
static const char kUnavailable[] = "unavailable configuration cannot be edited";
static const char kWrongType[] =
    "editor does not match the selected value type";
static const char kInvalidInteger[] =
    "integer editor requires a canonical decimal integer";
static const char kInvalidHex[] =
    "hex editor requires a non-negative 0x-prefixed integer";
static const char kInputTooLong[] = "UI input exceeds its bounded limit";
static const char kInvalidInput[] = "UI input contains an embedded NUL";
static const char kUnknownCommand[] = "unknown menuconfig command";
static const char kDirtyQuit[] =
    "configuration has unsaved changes; use :wq or :q!";
static const char kQuitHint[] = "use :q, :wq, or :q!";
static const char kSaveFailed[] = "save failed; working changes were preserved";
static const char kSaved[] = "configuration saved";
static const char kUndoEmpty[] = "nothing to undo";
static const char kRedoEmpty[] = "nothing to redo";
static const char kSearchMiss[] = "no matching configuration";
static const char kBufferTooSmall[] = "semantic UI view buffer is too small";
static const char kSaveNotPending[] = "no UI save request is pending";

static ConfitStatus confit_ui_fail(ConfitDiagnostic *diagnostic,
                                   ConfitStatus status, const char *message) {
  confit_diagnostic_set(diagnostic, status, 0, 0U, 0U, message);
  return status;
}

static int confit_ui_allocator(const ConfitAllocator *requested,
                               ConfitAllocator *resolved) {
  if (resolved == 0)
    return 0;
  if (requested == 0) {
    confit_allocator_default(resolved);
    return 1;
  }
  if (!confit_allocator_is_valid(requested))
    return 0;
  *resolved = *requested;
  return 1;
}

static int confit_ui_size_multiply(size_t left, size_t right, size_t *out) {
  if (out == 0 || (left != 0U && right > SIZE_MAX / left))
    return 0;
  *out = left * right;
  return 1;
}

static void confit_ui_history_entry_init(ConfitUiHistoryEntry *entry) {
  if (entry == 0)
    return;
  memset(entry, 0, sizeof(*entry));
  entry->config_index = CONFIT_INDEX_NONE;
  confit_value_init(&entry->before);
  confit_value_init(&entry->after);
}

static void confit_ui_history_entry_destroy(ConfitUiHistoryEntry *entry) {
  if (entry == 0)
    return;
  confit_value_destroy(&entry->after);
  confit_value_destroy(&entry->before);
  confit_ui_history_entry_init(entry);
}

static void confit_ui_values_destroy(ConfitUiModel *model,
                                     ConfitValue *values) {
  size_t index;
  if (model == 0 || values == 0)
    return;
  for (index = model->value_count; index > 0U; --index)
    confit_value_destroy(&values[index - 1U]);
  model->allocator.deallocate(model->allocator.context, values);
}

static ConfitStatus confit_ui_values_allocate(ConfitUiModel *model,
                                              ConfitValue **out_values,
                                              ConfitDiagnostic *diagnostic) {
  ConfitValue *values;
  size_t bytes;
  size_t index;
  *out_values = 0;
  if (model->value_count == 0U)
    return CONFIT_OK;
  if (!confit_ui_size_multiply(model->value_count, sizeof(*values), &bytes))
    return confit_ui_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
  values =
      (ConfitValue *)model->allocator.allocate(model->allocator.context, bytes);
  if (values == 0)
    return confit_ui_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
  memset(values, 0, bytes);
  for (index = 0U; index < model->value_count; ++index)
    confit_value_init(&values[index]);
  *out_values = values;
  return CONFIT_OK;
}

static ConfitStatus confit_ui_values_from_resolution(
    ConfitUiModel *model, const ConfitResolution *resolution,
    ConfitValue **out_values, ConfitDiagnostic *diagnostic) {
  ConfitValue *values = 0;
  ConfitStatus status;
  size_t index;
  status = confit_ui_values_allocate(model, &values, diagnostic);
  if (status != CONFIT_OK)
    return status;
  for (index = 0U; index < model->value_count; ++index) {
    ConfitConfigView config;
    const ConfitResolvedValue *resolved = 0;
    if (!confit_catalog_config_at(model->catalog, index, &config) ||
        !confit_resolution_find_value(resolution, config.symbol, &resolved)) {
      confit_ui_values_destroy(model, values);
      return confit_ui_fail(diagnostic, CONFIT_ERR_INTERNAL, kInvalidArgument);
    }
    status = confit_value_copy(&values[index], &resolved->effective_value,
                               &model->allocator, diagnostic);
    if (status != CONFIT_OK) {
      confit_ui_values_destroy(model, values);
      return status;
    }
  }
  *out_values = values;
  return CONFIT_OK;
}

static ConfitStatus confit_ui_values_copy(ConfitUiModel *model,
                                          const ConfitValue *source,
                                          ConfitValue **out_values,
                                          ConfitDiagnostic *diagnostic) {
  ConfitValue *values = 0;
  ConfitStatus status;
  size_t index;
  status = confit_ui_values_allocate(model, &values, diagnostic);
  if (status != CONFIT_OK)
    return status;
  for (index = 0U; index < model->value_count; ++index) {
    status = confit_value_copy(&values[index], &source[index],
                               &model->allocator, diagnostic);
    if (status != CONFIT_OK) {
      confit_ui_values_destroy(model, values);
      return status;
    }
  }
  *out_values = values;
  return CONFIT_OK;
}

static int confit_ui_value_dirty(const ConfitUiModel *model, size_t index) {
  return !confit_value_equal(&model->working[index], &model->saved[index]);
}

int confit_ui_dirty(const ConfitUiModel *model) {
  size_t index;
  if (model == 0)
    return 0;
  for (index = 0U; index < model->value_count; ++index) {
    if (confit_ui_value_dirty(model, index))
      return 1;
  }
  return 0;
}

static int confit_ui_config_available(const ConfitUiModel *model,
                                      size_t config_index) {
  ConfitConfigView config;
  const ConfitResolvedValue *resolved = 0;
  return confit_catalog_config_at(model->catalog, config_index, &config) &&
         confit_resolution_find_value(model->resolution, config.symbol,
                                      &resolved) &&
         resolved->available;
}

static size_t confit_ui_menu_depth(const ConfitUiModel *model,
                                   size_t menu_index) {
  size_t depth = 0U;
  while (menu_index != CONFIT_INDEX_NONE) {
    ConfitMenuView menu;
    if (!confit_catalog_menu_at(model->catalog, menu_index, &menu))
      return 0U;
    ++depth;
    menu_index = menu.parent_menu;
  }
  return depth;
}

static void confit_ui_adjust_viewport(ConfitUiModel *model) {
  if (model->row_count == 0U) {
    model->cursor = 0U;
    model->viewport_offset = 0U;
    return;
  }
  if (model->cursor >= model->row_count)
    model->cursor = model->row_count - 1U;
  if (model->cursor < model->viewport_offset)
    model->viewport_offset = model->cursor;
  if (model->viewport_rows != 0U &&
      model->cursor >= model->viewport_offset + model->viewport_rows)
    model->viewport_offset = model->cursor - model->viewport_rows + 1U;
  if (model->viewport_offset >= model->row_count)
    model->viewport_offset = model->row_count - 1U;
}

static void confit_ui_rebuild_rows(ConfitUiModel *model) {
  size_t index;
  model->row_count = 0U;
  for (index = 0U; index < confit_catalog_menu_count(model->catalog); ++index) {
    ConfitMenuView menu;
    if (confit_catalog_menu_at(model->catalog, index, &menu) &&
        menu.parent_menu == model->current_menu) {
      model->rows[model->row_count].kind = CONFIT_UI_ROW_MENU;
      model->rows[model->row_count].index = index;
      ++model->row_count;
    }
  }
  for (index = 0U; index < model->value_count; ++index) {
    ConfitConfigView config;
    if (confit_catalog_config_at(model->catalog, index, &config) &&
        config.menu == model->current_menu &&
        (model->show_unavailable || confit_ui_config_available(model, index))) {
      model->rows[model->row_count].kind = CONFIT_UI_ROW_CONFIG;
      model->rows[model->row_count].index = index;
      ++model->row_count;
    }
  }
  confit_ui_adjust_viewport(model);
}

static int confit_ui_selected_config(const ConfitUiModel *model,
                                     size_t *out_index) {
  if (model == 0 || out_index == 0 || model->cursor >= model->row_count ||
      model->rows[model->cursor].kind != CONFIT_UI_ROW_CONFIG)
    return 0;
  *out_index = model->rows[model->cursor].index;
  return 1;
}

static int confit_ui_select_config(ConfitUiModel *model, size_t config_index) {
  ConfitConfigView config;
  size_t row;
  if (!confit_catalog_config_at(model->catalog, config_index, &config))
    return 0;
  model->current_menu = config.menu;
  model->cursor = 0U;
  model->viewport_offset = 0U;
  confit_ui_rebuild_rows(model);
  for (row = 0U; row < model->row_count; ++row) {
    if (model->rows[row].kind == CONFIT_UI_ROW_CONFIG &&
        model->rows[row].index == config_index) {
      model->cursor = row;
      confit_ui_adjust_viewport(model);
      return 1;
    }
  }
  return 0;
}

static void confit_ui_assignments_destroy(ConfitAssignment *assignments,
                                          size_t count,
                                          const ConfitAllocator *allocator) {
  size_t index;
  if (assignments == 0)
    return;
  for (index = count; index > 0U; --index)
    confit_assignment_destroy(&assignments[index - 1U]);
  allocator->deallocate(allocator->context, assignments);
}

static ConfitStatus
confit_ui_resolve_candidate(ConfitUiModel *model, size_t changed_index,
                            const ConfitValue *changed_value,
                            ConfitResolution **out_resolution,
                            ConfitDiagnostic *diagnostic) {
  ConfitAssignment *assignments = 0;
  size_t assignment_count = 0U;
  size_t initialized = 0U;
  size_t bytes;
  size_t index;
  ConfitStatus status;
  *out_resolution = 0;
  if (model->value_count != 0U) {
    if (!confit_ui_size_multiply(model->value_count, sizeof(*assignments),
                                 &bytes))
      return confit_ui_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
    assignments = (ConfitAssignment *)model->allocator.allocate(
        model->allocator.context, bytes);
    if (assignments == 0)
      return confit_ui_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
    memset(assignments, 0, bytes);
    for (index = 0U; index < model->value_count; ++index) {
      ConfitConfigView config;
      const ConfitValue *value =
          index == changed_index ? changed_value : &model->working[index];
      if (!confit_catalog_config_at(model->catalog, index, &config)) {
        status =
            confit_ui_fail(diagnostic, CONFIT_ERR_INTERNAL, kInvalidArgument);
        goto done;
      }
      if (confit_value_equal(value, config.default_value))
        continue;
      confit_assignment_init(&assignments[assignment_count]);
      ++initialized;
      status =
          confit_assignment_set(&assignments[assignment_count], config.symbol,
                                value, &model->allocator, diagnostic);
      if (status != CONFIT_OK)
        goto done;
      ++assignment_count;
    }
  }
  status =
      confit_resolve(model->catalog, model->plan, assignments, assignment_count,
                     &model->allocator, out_resolution, diagnostic);

done:
  confit_ui_assignments_destroy(assignments, initialized, &model->allocator);
  return status;
}

static ConfitStatus confit_ui_history_prepare(ConfitUiModel *model,
                                              size_t config_index,
                                              const ConfitValue *before,
                                              const ConfitValue *after,
                                              ConfitUiHistoryEntry *entry,
                                              ConfitDiagnostic *diagnostic) {
  ConfitStatus status;
  confit_ui_history_entry_init(entry);
  entry->config_index = config_index;
  status =
      confit_value_copy(&entry->before, before, &model->allocator, diagnostic);
  if (status == CONFIT_OK)
    status =
        confit_value_copy(&entry->after, after, &model->allocator, diagnostic);
  if (status != CONFIT_OK)
    confit_ui_history_entry_destroy(entry);
  return status;
}

static void confit_ui_history_push(ConfitUiModel *model,
                                   ConfitUiHistoryEntry *entry) {
  while (model->history_count > model->history_position) {
    --model->history_count;
    confit_ui_history_entry_destroy(&model->history[model->history_count]);
  }
  if (model->history_count == CONFIT_UI_HISTORY_LIMIT) {
    confit_ui_history_entry_destroy(&model->history[0]);
    memmove(&model->history[0], &model->history[1],
            (CONFIT_UI_HISTORY_LIMIT - 1U) * sizeof(model->history[0]));
    confit_ui_history_entry_init(&model->history[CONFIT_UI_HISTORY_LIMIT - 1U]);
    --model->history_count;
    --model->history_position;
  }
  model->history[model->history_count] = *entry;
  confit_ui_history_entry_init(entry);
  ++model->history_count;
  model->history_position = model->history_count;
}

static ConfitStatus confit_ui_replace_value(ConfitUiModel *model,
                                            size_t config_index,
                                            const ConfitValue *value,
                                            int record_history,
                                            ConfitDiagnostic *diagnostic) {
  ConfitResolution *resolution = 0;
  ConfitValue replacement;
  ConfitUiHistoryEntry history;
  ConfitStatus status;
  confit_value_init(&replacement);
  confit_ui_history_entry_init(&history);
  if (config_index >= model->value_count)
    return confit_ui_fail(diagnostic, CONFIT_ERR_USAGE, kNoSelection);
  if (!confit_ui_config_available(model, config_index))
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kUnavailable);
  if (confit_value_equal(&model->working[config_index], value))
    return CONFIT_OK;
  status =
      confit_value_copy(&replacement, value, &model->allocator, diagnostic);
  if (status == CONFIT_OK && record_history)
    status = confit_ui_history_prepare(model, config_index,
                                       &model->working[config_index], value,
                                       &history, diagnostic);
  if (status == CONFIT_OK)
    status = confit_ui_resolve_candidate(model, config_index, value,
                                         &resolution, diagnostic);
  if (status != CONFIT_OK) {
    confit_ui_history_entry_destroy(&history);
    confit_value_destroy(&replacement);
    return status;
  }
  confit_value_destroy(&model->working[config_index]);
  model->working[config_index] = replacement;
  confit_value_init(&replacement);
  confit_resolution_destroy(model->resolution);
  model->resolution = resolution;
  if (record_history)
    confit_ui_history_push(model, &history);
  confit_ui_history_entry_destroy(&history);
  confit_ui_rebuild_rows(model);
  return CONFIT_OK;
}

static int confit_ui_ascii_fold(int character) {
  if (character >= 'A' && character <= 'Z')
    return character + ('a' - 'A');
  return character;
}

static int confit_ui_contains_folded(const char *text, const char *needle) {
  size_t start;
  size_t index;
  const size_t text_size = text != 0 ? strlen(text) : 0U;
  const size_t needle_size = needle != 0 ? strlen(needle) : 0U;
  if (needle_size == 0U)
    return 1;
  if (needle_size > text_size)
    return 0;
  for (start = 0U; start + needle_size <= text_size; ++start) {
    for (index = 0U; index < needle_size; ++index) {
      if (confit_ui_ascii_fold((unsigned char)text[start + index]) !=
          confit_ui_ascii_fold((unsigned char)needle[index]))
        break;
    }
    if (index == needle_size)
      return 1;
  }
  return 0;
}

static int confit_ui_config_matches(const ConfitConfigView *config,
                                    const char *query) {
  return confit_ui_contains_folded(config->symbol, query) ||
         confit_ui_contains_folded(config->prompt, query) ||
         confit_ui_contains_folded(config->help, query);
}

static ConfitStatus confit_ui_search(ConfitUiModel *model, int direction,
                                     ConfitDiagnostic *diagnostic) {
  size_t selected = CONFIT_INDEX_NONE;
  size_t step;
  if (model->input_size == 0U)
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kSearchMiss);
  (void)confit_ui_selected_config(model, &selected);
  for (step = 1U; step <= model->value_count; ++step) {
    size_t index;
    ConfitConfigView config;
    if (direction > 0) {
      const size_t base =
          selected == CONFIT_INDEX_NONE ? model->value_count - 1U : selected;
      index = (base + step) % model->value_count;
    } else {
      const size_t base = selected == CONFIT_INDEX_NONE ? 0U : selected;
      index = (base + model->value_count - (step % model->value_count)) %
              model->value_count;
    }
    if (!confit_catalog_config_at(model->catalog, index, &config))
      continue;
    if (!model->show_unavailable && !confit_ui_config_available(model, index))
      continue;
    if (confit_ui_config_matches(&config, model->input) &&
        confit_ui_select_config(model, index)) {
      model->state = CONFIT_UI_NORMAL;
      model->notice = 0;
      return CONFIT_OK;
    }
  }
  return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kSearchMiss);
}

static int confit_ui_parse_int(const char *text, size_t size,
                               int64_t *out_value) {
  uint64_t magnitude = 0U;
  uint64_t limit = (uint64_t)INT64_MAX;
  size_t index = 0U;
  int negative = 0;
  if (text == 0 || out_value == 0 || size == 0U)
    return 0;
  if (text[index] == '-') {
    negative = 1;
    limit += 1U;
    if (++index == size)
      return 0;
  }
  for (; index < size; ++index) {
    unsigned int digit;
    if (text[index] < '0' || text[index] > '9')
      return 0;
    digit = (unsigned int)(text[index] - '0');
    if (magnitude > (limit - digit) / 10U)
      return 0;
    magnitude = magnitude * 10U + digit;
  }
  if (negative) {
    *out_value =
        magnitude == (uint64_t)INT64_MAX + 1U ? INT64_MIN : -(int64_t)magnitude;
  } else {
    *out_value = (int64_t)magnitude;
  }
  return 1;
}

static int confit_ui_hex_digit(char character, unsigned int *out_digit) {
  if (character >= '0' && character <= '9')
    *out_digit = (unsigned int)(character - '0');
  else if (character >= 'a' && character <= 'f')
    *out_digit = (unsigned int)(character - 'a' + 10);
  else if (character >= 'A' && character <= 'F')
    *out_digit = (unsigned int)(character - 'A' + 10);
  else
    return 0;
  return 1;
}

static int confit_ui_parse_hex(const char *text, size_t size,
                               uint64_t *out_value) {
  uint64_t value = 0U;
  size_t index;
  int saw_digit = 0;
  int previous_underscore = 0;
  if (text == 0 || out_value == 0 || size < 3U || text[0] != '0' ||
      (text[1] != 'x' && text[1] != 'X'))
    return 0;
  for (index = 2U; index < size; ++index) {
    unsigned int digit;
    if (text[index] == '_') {
      if (!saw_digit || previous_underscore || index + 1U == size)
        return 0;
      previous_underscore = 1;
      continue;
    }
    if (!confit_ui_hex_digit(text[index], &digit) ||
        value > ((uint64_t)INT64_MAX - digit) / 16U)
      return 0;
    value = value * 16U + digit;
    saw_digit = 1;
    previous_underscore = 0;
  }
  if (!saw_digit || previous_underscore)
    return 0;
  *out_value = value;
  return 1;
}

static ConfitStatus confit_ui_begin_editor(ConfitUiModel *model,
                                           ConfitDiagnostic *diagnostic) {
  ConfitConfigView config;
  size_t config_index;
  if (!confit_ui_selected_config(model, &config_index) ||
      !confit_catalog_config_at(model->catalog, config_index, &config))
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kNoSelection);
  if (!confit_ui_config_available(model, config_index))
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kUnavailable);
  model->input[0] = '\0';
  model->input_size = 0U;
  model->notice = 0;
  if (config.kind == CONFIT_VALUE_BOOL)
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kWrongType);
  if (config.kind == CONFIT_VALUE_ENUM) {
    const char *text = 0;
    size_t text_size = 0U;
    size_t index;
    model->enum_selection = 0U;
    (void)confit_value_text(&model->working[config_index], &text, &text_size);
    for (index = 0U; index < config.enum_value_count; ++index) {
      if (strlen(config.enum_values[index]) == text_size &&
          memcmp(config.enum_values[index], text, text_size) == 0) {
        model->enum_selection = index;
        break;
      }
    }
    model->state = CONFIT_UI_ENUM_PICKER;
  } else {
    model->state = CONFIT_UI_EDIT;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_ui_accept_edit(ConfitUiModel *model,
                                          ConfitDiagnostic *diagnostic) {
  ConfitConfigView config;
  ConfitValue candidate;
  ConfitStatus status;
  size_t config_index;
  confit_value_init(&candidate);
  if (!confit_ui_selected_config(model, &config_index) ||
      !confit_catalog_config_at(model->catalog, config_index, &config))
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kNoSelection);
  switch (config.kind) {
  case CONFIT_VALUE_INT: {
    int64_t value;
    if (!confit_ui_parse_int(model->input, model->input_size, &value))
      return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidInteger);
    status =
        confit_value_set_int(&candidate, value, &model->allocator, diagnostic);
    break;
  }
  case CONFIT_VALUE_HEX: {
    uint64_t value;
    if (!confit_ui_parse_hex(model->input, model->input_size, &value))
      return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidHex);
    status =
        confit_value_set_hex(&candidate, value, &model->allocator, diagnostic);
    break;
  }
  case CONFIT_VALUE_STRING:
    status =
        confit_value_set_string(&candidate, model->input, model->input_size,
                                &model->allocator, diagnostic);
    break;
  default:
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kWrongType);
  }
  if (status == CONFIT_OK)
    status =
        confit_ui_replace_value(model, config_index, &candidate, 1, diagnostic);
  confit_value_destroy(&candidate);
  if (status == CONFIT_OK) {
    model->state = CONFIT_UI_NORMAL;
    model->input[0] = '\0';
    model->input_size = 0U;
  }
  return status;
}

static ConfitStatus confit_ui_accept_enum(ConfitUiModel *model,
                                          ConfitDiagnostic *diagnostic) {
  ConfitConfigView config;
  ConfitValue candidate;
  ConfitStatus status;
  size_t config_index;
  confit_value_init(&candidate);
  if (!confit_ui_selected_config(model, &config_index) ||
      !confit_catalog_config_at(model->catalog, config_index, &config) ||
      config.kind != CONFIT_VALUE_ENUM ||
      model->enum_selection >= config.enum_value_count)
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kWrongType);
  status = confit_value_set_enum(
      &candidate, config.enum_values[model->enum_selection],
      strlen(config.enum_values[model->enum_selection]), &model->allocator,
      diagnostic);
  if (status == CONFIT_OK)
    status =
        confit_ui_replace_value(model, config_index, &candidate, 1, diagnostic);
  confit_value_destroy(&candidate);
  if (status == CONFIT_OK)
    model->state = CONFIT_UI_NORMAL;
  return status;
}

static ConfitStatus confit_ui_command(ConfitUiModel *model,
                                      ConfitUiEffect *out_effect,
                                      ConfitDiagnostic *diagnostic) {
  if (strcmp(model->input, ":w") == 0) {
    ConfitStatus status = confit_ui_values_copy(
        model, model->working, &model->pending_saved, diagnostic);
    if (status != CONFIT_OK)
      return status;
    model->save_pending = 1;
    model->exit_after_save = 0;
    *out_effect = CONFIT_UI_EFFECT_REQUEST_SAVE;
  } else if (strcmp(model->input, ":q") == 0) {
    if (confit_ui_dirty(model))
      return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kDirtyQuit);
    *out_effect = CONFIT_UI_EFFECT_EXIT;
  } else if (strcmp(model->input, ":q!") == 0) {
    *out_effect = CONFIT_UI_EFFECT_DISCARD_AND_EXIT;
  } else if (strcmp(model->input, ":wq") == 0) {
    ConfitStatus status = confit_ui_values_copy(
        model, model->working, &model->pending_saved, diagnostic);
    if (status != CONFIT_OK)
      return status;
    model->save_pending = 1;
    model->exit_after_save = 1;
    *out_effect = CONFIT_UI_EFFECT_REQUEST_SAVE;
  } else if (strcmp(model->input, ":x") == 0) {
    if (confit_ui_dirty(model)) {
      ConfitStatus status = confit_ui_values_copy(
          model, model->working, &model->pending_saved, diagnostic);
      if (status != CONFIT_OK)
        return status;
      model->save_pending = 1;
      model->exit_after_save = 1;
      *out_effect = CONFIT_UI_EFFECT_REQUEST_SAVE;
    } else {
      *out_effect = CONFIT_UI_EFFECT_EXIT;
    }
  } else if (strcmp(model->input, ":help") == 0) {
    model->state = CONFIT_UI_HELP;
    return CONFIT_OK;
  } else if (strcmp(model->input, ":set unavailable") == 0) {
    model->show_unavailable = 1;
    confit_ui_rebuild_rows(model);
  } else if (strcmp(model->input, ":set nounavailable") == 0) {
    model->show_unavailable = 0;
    confit_ui_rebuild_rows(model);
  } else {
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kUnknownCommand);
  }
  model->state = CONFIT_UI_NORMAL;
  model->input[0] = '\0';
  model->input_size = 0U;
  return CONFIT_OK;
}

ConfitStatus
confit_ui_create(const ConfitCatalog *catalog, const ConfitDependencyPlan *plan,
                 const ConfitAssignment *assignments, size_t assignment_count,
                 const ConfitAllocator *allocator, ConfitUiModel **out_model,
                 ConfitDiagnostic *diagnostic) {
  ConfitAllocator resolved_allocator;
  ConfitUiModel *model;
  ConfitStatus status;
  size_t row_count;
  size_t bytes;
  size_t index;
  if (catalog == 0 || plan == 0 || out_model == 0 ||
      (assignment_count != 0U && assignments == 0))
    return confit_ui_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  *out_model = 0;
  if (!confit_dependency_plan_matches_catalog(plan, catalog))
    return confit_ui_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  if (!confit_ui_allocator(allocator, &resolved_allocator))
    return confit_ui_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidAllocator);
  model = (ConfitUiModel *)resolved_allocator.allocate(
      resolved_allocator.context, sizeof(*model));
  if (model == 0)
    return confit_ui_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
  memset(model, 0, sizeof(*model));
  model->allocator = resolved_allocator;
  model->catalog = catalog;
  model->plan = plan;
  model->value_count = confit_catalog_config_count(catalog);
  model->current_menu = CONFIT_INDEX_NONE;
  model->viewport_rows = 1U;
  model->state = CONFIT_UI_NORMAL;
  model->show_unavailable = 1;
  model->enum_selection = CONFIT_INDEX_NONE;
  for (index = 0U; index < CONFIT_UI_HISTORY_LIMIT; ++index)
    confit_ui_history_entry_init(&model->history[index]);
  status = confit_resolve(catalog, plan, assignments, assignment_count,
                          &resolved_allocator, &model->resolution, diagnostic);
  if (status != CONFIT_OK)
    goto fail;
  status = confit_ui_values_from_resolution(model, model->resolution,
                                            &model->working, diagnostic);
  if (status != CONFIT_OK)
    goto fail;
  status = confit_ui_values_from_resolution(model, model->resolution,
                                            &model->saved, diagnostic);
  if (status != CONFIT_OK)
    goto fail;
  row_count = confit_catalog_menu_count(catalog) + model->value_count;
  model->row_capacity = row_count;
  if (row_count != 0U) {
    if (!confit_ui_size_multiply(row_count, sizeof(*model->rows), &bytes)) {
      status = confit_ui_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
      goto fail;
    }
    model->rows = (ConfitUiRow *)resolved_allocator.allocate(
        resolved_allocator.context, bytes);
    if (model->rows == 0) {
      status = confit_ui_fail(diagnostic, CONFIT_ERR_INTERNAL, kOutOfMemory);
      goto fail;
    }
  }
  confit_ui_rebuild_rows(model);
  *out_model = model;
  return CONFIT_OK;

fail:
  confit_ui_destroy(model);
  return status;
}

void confit_ui_destroy(ConfitUiModel *model) {
  ConfitAllocator allocator;
  size_t index;
  if (model == 0)
    return;
  allocator = model->allocator;
  for (index = CONFIT_UI_HISTORY_LIMIT; index > 0U; --index)
    confit_ui_history_entry_destroy(&model->history[index - 1U]);
  if (model->rows != 0)
    allocator.deallocate(allocator.context, model->rows);
  confit_ui_values_destroy(model, model->saved);
  confit_ui_values_destroy(model, model->pending_saved);
  confit_ui_values_destroy(model, model->working);
  confit_resolution_destroy(model->resolution);
  memset(model, 0, sizeof(*model));
  allocator.deallocate(allocator.context, model);
}

ConfitUiState confit_ui_state(const ConfitUiModel *model) {
  return model != 0 ? model->state : CONFIT_UI_NORMAL;
}

int confit_ui_show_unavailable(const ConfitUiModel *model) {
  return model != 0 ? model->show_unavailable : 0;
}

const char *confit_ui_notice(const ConfitUiModel *model) {
  return model != 0 ? model->notice : 0;
}

size_t confit_ui_row_count(const ConfitUiModel *model) {
  return model != 0 ? model->row_count : 0U;
}

size_t confit_ui_cursor(const ConfitUiModel *model) {
  return model != 0 ? model->cursor : 0U;
}

size_t confit_ui_viewport_offset(const ConfitUiModel *model) {
  return model != 0 ? model->viewport_offset : 0U;
}

size_t confit_ui_viewport_rows(const ConfitUiModel *model) {
  return model != 0 ? model->viewport_rows : 0U;
}

ConfitStatus confit_ui_set_viewport_rows(ConfitUiModel *model, size_t rows,
                                         ConfitDiagnostic *diagnostic) {
  if (model == 0 || rows == 0U || rows > CONFIT_LIMIT_RENDER_ROWS)
    return confit_ui_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  model->viewport_rows = rows;
  confit_ui_adjust_viewport(model);
  return CONFIT_OK;
}

int confit_ui_row_at(const ConfitUiModel *model, size_t row_index,
                     ConfitUiRowView *out_view) {
  const ConfitUiRow *row;
  if (model == 0 || out_view == 0 || row_index >= model->row_count)
    return 0;
  memset(out_view, 0, sizeof(*out_view));
  out_view->menu_index = CONFIT_INDEX_NONE;
  out_view->config_index = CONFIT_INDEX_NONE;
  row = &model->rows[row_index];
  out_view->kind = row->kind;
  if (row->kind == CONFIT_UI_ROW_MENU) {
    ConfitMenuView menu;
    if (!confit_catalog_menu_at(model->catalog, row->index, &menu))
      return 0;
    out_view->menu_index = row->index;
    out_view->depth = confit_ui_menu_depth(model, row->index);
    out_view->prompt = menu.prompt;
    out_view->help = menu.help;
    out_view->available = 1;
  } else {
    ConfitConfigView config;
    const ConfitResolvedValue *resolved = 0;
    if (!confit_catalog_config_at(model->catalog, row->index, &config) ||
        !confit_resolution_find_value(model->resolution, config.symbol,
                                      &resolved))
      return 0;
    out_view->config_index = row->index;
    out_view->depth = config.menu == CONFIT_INDEX_NONE
                          ? 0U
                          : confit_ui_menu_depth(model, config.menu);
    out_view->prompt = config.prompt;
    out_view->help = config.help;
    out_view->symbol = config.symbol;
    out_view->value_kind = config.kind;
    out_view->effective_value = &resolved->effective_value;
    out_view->default_value = config.default_value;
    out_view->origin = resolved->origin;
    out_view->available = resolved->available;
  }
  return 1;
}

size_t confit_ui_diff_count(const ConfitUiModel *model) {
  size_t count = 0U;
  size_t index;
  if (model == 0)
    return 0U;
  for (index = 0U; index < model->value_count; ++index)
    if (confit_ui_value_dirty(model, index))
      ++count;
  return count;
}

int confit_ui_diff_at(const ConfitUiModel *model, size_t diff_index,
                      ConfitUiDiffView *out_view) {
  size_t seen = 0U;
  size_t index;
  if (model == 0 || out_view == 0)
    return 0;
  for (index = 0U; index < model->value_count; ++index) {
    ConfitConfigView config;
    if (!confit_ui_value_dirty(model, index))
      continue;
    if (seen++ != diff_index)
      continue;
    if (!confit_catalog_config_at(model->catalog, index, &config))
      return 0;
    out_view->config_index = index;
    out_view->symbol = config.symbol;
    out_view->saved_value = &model->saved[index];
    out_view->working_value = &model->working[index];
    return 1;
  }
  return 0;
}

static ConfitStatus confit_ui_toggle(ConfitUiModel *model,
                                     ConfitDiagnostic *diagnostic) {
  ConfitConfigView config;
  ConfitValue candidate;
  ConfitStatus status;
  size_t config_index;
  confit_value_init(&candidate);
  if (!confit_ui_selected_config(model, &config_index) ||
      !confit_catalog_config_at(model->catalog, config_index, &config))
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kNoSelection);
  if (config.kind != CONFIT_VALUE_BOOL)
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kWrongType);
  status = confit_value_set_bool(&candidate,
                                 !model->working[config_index].data.boolean,
                                 &model->allocator, diagnostic);
  if (status == CONFIT_OK)
    status =
        confit_ui_replace_value(model, config_index, &candidate, 1, diagnostic);
  confit_value_destroy(&candidate);
  return status;
}

static ConfitStatus confit_ui_history_apply(ConfitUiModel *model, int redo,
                                            ConfitDiagnostic *diagnostic) {
  ConfitUiHistoryEntry *entry;
  const ConfitValue *value;
  ConfitStatus status;
  if (!redo && model->history_position == 0U)
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kUndoEmpty);
  if (redo && model->history_position == model->history_count)
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kRedoEmpty);
  entry = redo ? &model->history[model->history_position]
               : &model->history[model->history_position - 1U];
  value = redo ? &entry->after : &entry->before;
  status =
      confit_ui_replace_value(model, entry->config_index, value, 0, diagnostic);
  if (status == CONFIT_OK) {
    if (redo)
      ++model->history_position;
    else
      --model->history_position;
    (void)confit_ui_select_config(model, entry->config_index);
  }
  return status;
}

ConfitStatus confit_ui_action(ConfitUiModel *model, ConfitUiAction action,
                              ConfitUiEffect *out_effect,
                              ConfitDiagnostic *diagnostic) {
  if (model == 0 || out_effect == 0)
    return confit_ui_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  *out_effect = CONFIT_UI_EFFECT_NONE;
  model->notice = 0;
  if (model->save_pending)
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidAction);
  if (action == CONFIT_UI_ACTION_CANCEL) {
    model->state = CONFIT_UI_NORMAL;
    model->input[0] = '\0';
    model->input_size = 0U;
    return CONFIT_OK;
  }
  if (model->state == CONFIT_UI_ENUM_PICKER) {
    ConfitConfigView config;
    size_t config_index;
    if (!confit_ui_selected_config(model, &config_index) ||
        !confit_catalog_config_at(model->catalog, config_index, &config) ||
        config.kind != CONFIT_VALUE_ENUM)
      return confit_ui_fail(diagnostic, CONFIT_ERR_INTERNAL, kWrongType);
    if (action == CONFIT_UI_ACTION_NEXT && config.enum_value_count != 0U) {
      model->enum_selection =
          (model->enum_selection + 1U) % config.enum_value_count;
      return CONFIT_OK;
    }
    if (action == CONFIT_UI_ACTION_PREVIOUS && config.enum_value_count != 0U) {
      model->enum_selection =
          (model->enum_selection + config.enum_value_count - 1U) %
          config.enum_value_count;
      return CONFIT_OK;
    }
    if (action == CONFIT_UI_ACTION_OPEN)
      return confit_ui_accept_enum(model, diagnostic);
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidAction);
  }
  if (model->state != CONFIT_UI_NORMAL)
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidAction);
  switch (action) {
  case CONFIT_UI_ACTION_NEXT:
    if (model->row_count != 0U && model->cursor + 1U < model->row_count)
      ++model->cursor;
    confit_ui_adjust_viewport(model);
    return CONFIT_OK;
  case CONFIT_UI_ACTION_PREVIOUS:
    if (model->cursor != 0U)
      --model->cursor;
    confit_ui_adjust_viewport(model);
    return CONFIT_OK;
  case CONFIT_UI_ACTION_PARENT:
    if (model->current_menu != CONFIT_INDEX_NONE) {
      ConfitMenuView menu;
      const size_t previous = model->current_menu;
      if (!confit_catalog_menu_at(model->catalog, previous, &menu))
        return confit_ui_fail(diagnostic, CONFIT_ERR_INTERNAL,
                              kInvalidArgument);
      model->current_menu = menu.parent_menu;
      model->cursor = 0U;
      model->viewport_offset = 0U;
      confit_ui_rebuild_rows(model);
      {
        size_t row;
        for (row = 0U; row < model->row_count; ++row)
          if (model->rows[row].kind == CONFIT_UI_ROW_MENU &&
              model->rows[row].index == previous)
            model->cursor = row;
      }
      confit_ui_adjust_viewport(model);
    }
    return CONFIT_OK;
  case CONFIT_UI_ACTION_OPEN:
    if (model->cursor >= model->row_count)
      return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kNoSelection);
    if (model->rows[model->cursor].kind == CONFIT_UI_ROW_MENU) {
      model->current_menu = model->rows[model->cursor].index;
      model->cursor = 0U;
      model->viewport_offset = 0U;
      confit_ui_rebuild_rows(model);
      return CONFIT_OK;
    }
    {
      size_t config_index;
      ConfitConfigView config;
      if (!confit_ui_selected_config(model, &config_index) ||
          !confit_catalog_config_at(model->catalog, config_index, &config))
        return confit_ui_fail(diagnostic, CONFIT_ERR_INTERNAL, kNoSelection);
      if (config.kind == CONFIT_VALUE_BOOL)
        return confit_ui_toggle(model, diagnostic);
    }
    return confit_ui_begin_editor(model, diagnostic);
  case CONFIT_UI_ACTION_TOGGLE:
    return confit_ui_toggle(model, diagnostic);
  case CONFIT_UI_ACTION_BEGIN_EDIT:
    return confit_ui_begin_editor(model, diagnostic);
  case CONFIT_UI_ACTION_BEGIN_SEARCH:
    model->state = CONFIT_UI_SEARCH;
    model->input[0] = '\0';
    model->input_size = 0U;
    return CONFIT_OK;
  case CONFIT_UI_ACTION_SEARCH_NEXT:
    return confit_ui_search(model, 1, diagnostic);
  case CONFIT_UI_ACTION_SEARCH_PREVIOUS:
    return confit_ui_search(model, -1, diagnostic);
  case CONFIT_UI_ACTION_UNDO:
    return confit_ui_history_apply(model, 0, diagnostic);
  case CONFIT_UI_ACTION_REDO:
    return confit_ui_history_apply(model, 1, diagnostic);
  case CONFIT_UI_ACTION_SHOW_DIFF:
    model->state = CONFIT_UI_DIFF;
    return CONFIT_OK;
  case CONFIT_UI_ACTION_SHOW_HELP:
    model->state = CONFIT_UI_HELP;
    return CONFIT_OK;
  case CONFIT_UI_ACTION_BEGIN_COMMAND:
    model->state = CONFIT_UI_COMMAND;
    model->input[0] = '\0';
    model->input_size = 0U;
    return CONFIT_OK;
  case CONFIT_UI_ACTION_QUIT_HINT:
    model->notice = kQuitHint;
    return CONFIT_OK;
  default:
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidAction);
  }
}

ConfitStatus confit_ui_set_input(ConfitUiModel *model, const char *text,
                                 size_t text_size,
                                 ConfitDiagnostic *diagnostic) {
  size_t limit;
  if (model == 0 || (text == 0 && text_size != 0U) ||
      (model->state != CONFIT_UI_EDIT && model->state != CONFIT_UI_SEARCH &&
       model->state != CONFIT_UI_COMMAND))
    return confit_ui_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidAction);
  limit = model->state == CONFIT_UI_COMMAND ? CONFIT_UI_COMMAND_LIMIT
                                            : CONFIT_LIMIT_STRING_BYTES;
  if (text_size > limit)
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kInputTooLong);
  if (text_size != 0U && memchr(text, '\0', text_size) != 0)
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidInput);
  if (text_size != 0U)
    memcpy(model->input, text, text_size);
  model->input[text_size] = '\0';
  model->input_size = text_size;
  return CONFIT_OK;
}

const char *confit_ui_input(const ConfitUiModel *model, size_t *out_size) {
  if (out_size != 0)
    *out_size = model != 0 ? model->input_size : 0U;
  return model != 0 ? model->input : 0;
}

ConfitStatus confit_ui_accept(ConfitUiModel *model, ConfitUiEffect *out_effect,
                              ConfitDiagnostic *diagnostic) {
  if (model == 0 || out_effect == 0)
    return confit_ui_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  *out_effect = CONFIT_UI_EFFECT_NONE;
  model->notice = 0;
  switch (model->state) {
  case CONFIT_UI_EDIT:
    return confit_ui_accept_edit(model, diagnostic);
  case CONFIT_UI_SEARCH:
    return confit_ui_search(model, 1, diagnostic);
  case CONFIT_UI_COMMAND:
    return confit_ui_command(model, out_effect, diagnostic);
  case CONFIT_UI_ENUM_PICKER:
    return confit_ui_accept_enum(model, diagnostic);
  default:
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kInvalidAction);
  }
}

ConfitStatus confit_ui_enum_select(ConfitUiModel *model, size_t value_index,
                                   ConfitDiagnostic *diagnostic) {
  ConfitConfigView config;
  size_t config_index;
  if (model == 0 || model->state != CONFIT_UI_ENUM_PICKER ||
      !confit_ui_selected_config(model, &config_index) ||
      !confit_catalog_config_at(model->catalog, config_index, &config) ||
      config.kind != CONFIT_VALUE_ENUM ||
      value_index >= config.enum_value_count)
    return confit_ui_fail(diagnostic, CONFIT_ERR_VALIDATION, kWrongType);
  model->enum_selection = value_index;
  return CONFIT_OK;
}

size_t confit_ui_enum_selection(const ConfitUiModel *model) {
  return model != 0 ? model->enum_selection : CONFIT_INDEX_NONE;
}

ConfitStatus confit_ui_save_result(ConfitUiModel *model, int succeeded,
                                   ConfitUiEffect *out_effect,
                                   ConfitDiagnostic *diagnostic) {
  if (model == 0 || out_effect == 0)
    return confit_ui_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  *out_effect = CONFIT_UI_EFFECT_NONE;
  if (!model->save_pending)
    return confit_ui_fail(diagnostic, CONFIT_ERR_USAGE, kSaveNotPending);
  if (!succeeded) {
    confit_ui_values_destroy(model, model->pending_saved);
    model->pending_saved = 0;
    model->save_pending = 0;
    model->exit_after_save = 0;
    model->notice = kSaveFailed;
    return CONFIT_OK;
  }
  confit_ui_values_destroy(model, model->saved);
  model->saved = model->pending_saved;
  model->pending_saved = 0;
  model->save_pending = 0;
  if (model->exit_after_save)
    *out_effect = CONFIT_UI_EFFECT_EXIT;
  model->exit_after_save = 0;
  model->notice = kSaved;
  return CONFIT_OK;
}

const ConfitResolution *confit_ui_resolution(const ConfitUiModel *model) {
  return model != 0 ? model->resolution : 0;
}

typedef struct ConfitUiWriter {
  char *buffer;
  size_t capacity;
  size_t cursor;
  int valid;
} ConfitUiWriter;

static void confit_ui_writer_bytes(ConfitUiWriter *writer, const char *text,
                                   size_t size) {
  if (!writer->valid || writer->cursor > SIZE_MAX - size) {
    writer->valid = 0;
    return;
  }
  if (writer->buffer != 0) {
    if (writer->cursor + size > writer->capacity) {
      writer->valid = 0;
      return;
    }
    if (size != 0U)
      memcpy(writer->buffer + writer->cursor, text, size);
  }
  writer->cursor += size;
}

static void confit_ui_writer_text(ConfitUiWriter *writer, const char *text) {
  confit_ui_writer_bytes(writer, text, strlen(text));
}

static void confit_ui_writer_number(ConfitUiWriter *writer, size_t value) {
  char buffer[32];
  const int length = snprintf(buffer, sizeof(buffer), "%zu", value);
  if (length < 0 || (size_t)length >= sizeof(buffer)) {
    writer->valid = 0;
    return;
  }
  confit_ui_writer_bytes(writer, buffer, (size_t)length);
}

static void confit_ui_format_view_pass(const ConfitUiModel *model,
                                       ConfitUiWriter *writer) {
  size_t index;
  confit_ui_writer_text(writer, "state=");
  confit_ui_writer_number(writer, (size_t)model->state);
  confit_ui_writer_text(writer, " dirty=");
  confit_ui_writer_number(writer, (size_t)confit_ui_dirty(model));
  confit_ui_writer_text(writer, " menu=");
  if (model->current_menu == CONFIT_INDEX_NONE)
    confit_ui_writer_text(writer, "root");
  else
    confit_ui_writer_number(writer, model->current_menu);
  confit_ui_writer_text(writer, " cursor=");
  confit_ui_writer_number(writer, model->cursor);
  confit_ui_writer_text(writer, "\n");
  for (index = 0U; index < model->row_count; ++index) {
    ConfitUiRowView row;
    if (!confit_ui_row_at(model, index, &row)) {
      writer->valid = 0;
      return;
    }
    confit_ui_writer_text(writer, index == model->cursor ? ">" : " ");
    confit_ui_writer_text(writer,
                          row.kind == CONFIT_UI_ROW_MENU ? "menu " : "config ");
    confit_ui_writer_number(writer, row.depth);
    confit_ui_writer_text(writer, " ");
    if (row.kind == CONFIT_UI_ROW_MENU) {
      confit_ui_writer_text(writer, row.prompt);
    } else {
      char value[CONFIT_LIMIT_STRING_BYTES + 64U];
      size_t value_size = 0U;
      if (confit_value_format_canonical(row.effective_value, value,
                                        sizeof(value), &value_size,
                                        0) != CONFIT_OK) {
        writer->valid = 0;
        return;
      }
      confit_ui_writer_text(writer, row.symbol);
      confit_ui_writer_text(writer,
                            row.available ? " available " : " unavailable ");
      confit_ui_writer_bytes(writer, value, value_size);
    }
    confit_ui_writer_text(writer, "\n");
  }
}

ConfitStatus confit_ui_format_view(const ConfitUiModel *model, char *buffer,
                                   size_t buffer_size, size_t *out_size,
                                   ConfitDiagnostic *diagnostic) {
  ConfitUiWriter measure;
  ConfitUiWriter output;
  if (model == 0 || out_size == 0 || (buffer == 0 && buffer_size != 0U))
    return confit_ui_fail(diagnostic, CONFIT_ERR_USAGE, kInvalidArgument);
  memset(&measure, 0, sizeof(measure));
  measure.valid = 1;
  confit_ui_format_view_pass(model, &measure);
  if (!measure.valid)
    return confit_ui_fail(diagnostic, CONFIT_ERR_INTERNAL, kInvalidArgument);
  *out_size = measure.cursor;
  if (buffer == 0 || buffer_size <= measure.cursor)
    return confit_ui_fail(diagnostic, CONFIT_ERR_USAGE, kBufferTooSmall);
  memset(&output, 0, sizeof(output));
  output.buffer = buffer;
  output.capacity = buffer_size - 1U;
  output.valid = 1;
  confit_ui_format_view_pass(model, &output);
  if (!output.valid || output.cursor != measure.cursor)
    return confit_ui_fail(diagnostic, CONFIT_ERR_INTERNAL, kInvalidArgument);
  buffer[output.cursor] = '\0';
  return CONFIT_OK;
}
