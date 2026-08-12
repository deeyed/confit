#include "confit/workflow_v5.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { CONFIT_WORKFLOW_MAX_ASSIGNMENTS = 512U };

typedef struct WorkflowAssignment {
  char *symbol;
  char *value;
  ConfitV5SourceSpan source;
} WorkflowAssignment;

typedef struct WorkflowText {
  char *bytes;
  size_t size;
  size_t capacity;
} WorkflowText;

struct ConfitV5Workflow {
  ConfitV5Catalog *catalog;
  ConfitV5Evaluation *evaluation;
  WorkflowAssignment *assignments;
  size_t assignment_count;
  char *architecture;
  char *kernconf;
};

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
  confit_diagnostic_set(diagnostic, status, path, 1U, 1U, message);
}

static ConfitStatus text_reserve(WorkflowText *text, size_t extra) {
  size_t needed;
  size_t next;
  char *grown;
  if (extra > CONFIT_V5_WORKFLOW_TEXT_MAX - text->size - 1U)
    return CONFIT_ERR_SCHEMA;
  needed = text->size + extra + 1U;
  if (needed <= text->capacity) return CONFIT_OK;
  next = text->capacity == 0U ? 512U : text->capacity;
  while (next < needed) {
    if (next > CONFIT_V5_WORKFLOW_TEXT_MAX / 2U) {
      next = CONFIT_V5_WORKFLOW_TEXT_MAX;
      break;
    }
    next *= 2U;
  }
  grown = (char *)realloc(text->bytes, next);
  if (grown == 0) return CONFIT_ERR_INTERNAL;
  text->bytes = grown;
  text->capacity = next;
  return CONFIT_OK;
}

static ConfitStatus text_add(WorkflowText *text, const char *value) {
  size_t size = strlen(value);
  ConfitStatus status = text_reserve(text, size);
  if (status != CONFIT_OK) return status;
  memcpy(text->bytes + text->size, value, size);
  text->size += size;
  text->bytes[text->size] = '\0';
  return CONFIT_OK;
}

static ConfitStatus text_printf(WorkflowText *text, const char *format, ...) {
  char buffer[2048];
  va_list arguments;
  int length;
  va_start(arguments, format);
  length = vsnprintf(buffer, sizeof(buffer), format, arguments);
  va_end(arguments);
  if (length < 0 || (size_t)length >= sizeof(buffer)) return CONFIT_ERR_SCHEMA;
  return text_add(text, buffer);
}

static ConfitStatus text_quote(WorkflowText *text, const char *value) {
  ConfitStatus status = text_add(text, "\"");
  for (size_t index = 0U; status == CONFIT_OK && value[index] != '\0'; ++index) {
    char escaped[3] = {'\\', value[index], '\0'};
    char plain[2] = {value[index], '\0'};
    unsigned char byte = (unsigned char)value[index];
    if (value[index] == '\\' || value[index] == '\"')
      status = text_add(text, escaped);
    else if (value[index] == '\n')
      status = text_add(text, "\\n");
    else if (value[index] == '\r')
      status = text_add(text, "\\r");
    else if (value[index] == '\t')
      status = text_add(text, "\\t");
    else if (byte < 0x20U || byte == 0x7fU)
      return CONFIT_ERR_SCHEMA;
    else
      status = text_add(text, plain);
  }
  return status == CONFIT_OK ? text_add(text, "\"") : status;
}

static int assignment_index(const ConfitV5Workflow *workflow,
                            const char *symbol) {
  for (size_t index = 0U; index < workflow->assignment_count; ++index)
    if (strcmp(workflow->assignments[index].symbol, symbol) == 0)
      return (int)index;
  return -1;
}

static int value_enabled(const ConfitV5Workflow *workflow,
                         const char *symbol) {
  for (size_t index = 0U;
       index < confit_v5_evaluation_value_count(workflow->evaluation); ++index) {
    const char *candidate = 0;
    const char *value = 0;
    int enabled = 0;
    ConfitV5SourceSpan source;
    if (confit_v5_evaluation_value_at(workflow->evaluation, index, &candidate,
                                      &value, &enabled, &source) &&
        strcmp(candidate, symbol) == 0)
      return enabled;
  }
  return 0;
}

static int row_compare(const void *left_pointer, const void *right_pointer) {
  const ConfitV5OptionView *left = (const ConfitV5OptionView *)left_pointer;
  const ConfitV5OptionView *right = (const ConfitV5OptionView *)right_pointer;
  int menu = strcmp(left->menu, right->menu);
  if (menu != 0) return menu;
  if (left->menu_order < right->menu_order) return -1;
  if (left->menu_order > right->menu_order) return 1;
  return strcmp(left->symbol, right->symbol);
}

static int workflow_option_at(const ConfitV5Workflow *workflow, size_t index,
                              ConfitV5OptionView *out) {
  ConfitV5OptionView *options;
  size_t count = confit_v5_catalog_option_count(workflow->catalog);
  int result = 0;
  if (index >= count || out == 0) return 0;
  options = (ConfitV5OptionView *)calloc(count, sizeof(options[0]));
  if (options == 0) return 0;
  for (size_t cursor = 0U; cursor < count; ++cursor)
    if (!confit_v5_catalog_option_at(workflow->catalog, cursor,
                                     &options[cursor]))
      goto done;
  qsort(options, count, sizeof(options[0]), row_compare);
  *out = options[index];
  result = 1;
done:
  free(options);
  return result;
}

static ConfitStatus evaluate_assignments(ConfitV5Workflow *workflow,
                                         ConfitDiagnostic *diagnostic) {
  ConfitV5Assignment values[CONFIT_WORKFLOW_MAX_ASSIGNMENTS];
  ConfitV5Evaluation *next = 0;
  ConfitStatus status;
  if (workflow->assignment_count > CONFIT_WORKFLOW_MAX_ASSIGNMENTS)
    return CONFIT_ERR_SCHEMA;
  for (size_t index = 0U; index < workflow->assignment_count; ++index) {
    values[index].symbol = workflow->assignments[index].symbol;
    values[index].value = workflow->assignments[index].value;
    values[index].source = workflow->assignments[index].source;
  }
  status = confit_v5_evaluate(workflow->catalog, values,
                              workflow->assignment_count, &next, diagnostic);
  if (next != 0) {
    confit_v5_evaluation_free(workflow->evaluation);
    workflow->evaluation = next;
  }
  return status;
}

ConfitStatus confit_v5_workflow_open(const ConfitV5CatalogRequest *request,
                                     ConfitV5Workflow **out_workflow,
                                     ConfitDiagnostic *diagnostic) {
  ConfitV5Workflow *workflow;
  ConfitStatus status;
  if (request == 0 || out_workflow == 0) return CONFIT_ERR_INVALID_ARGUMENT;
  *out_workflow = 0;
  workflow = (ConfitV5Workflow *)calloc(1U, sizeof(*workflow));
  if (workflow == 0) return CONFIT_ERR_INTERNAL;
  status = confit_v5_catalog_load(request, &workflow->catalog, diagnostic);
  if (status != CONFIT_OK) goto fail;
  workflow->architecture = copy_text(request->architecture);
  workflow->kernconf = copy_text(request->kernconf);
  workflow->assignment_count =
      confit_v5_catalog_assignment_count(workflow->catalog);
  if (workflow->assignment_count != 0U) {
    workflow->assignments = (WorkflowAssignment *)calloc(
        workflow->assignment_count, sizeof(workflow->assignments[0]));
    if (workflow->assignments == 0) {
      status = CONFIT_ERR_INTERNAL;
      goto fail;
    }
  }
  for (size_t index = 0U; index < workflow->assignment_count; ++index) {
    ConfitV5Assignment assignment;
    if (!confit_v5_catalog_assignment(workflow->catalog, index, &assignment)) {
      status = CONFIT_ERR_INTERNAL;
      goto fail;
    }
    workflow->assignments[index].symbol = copy_text(assignment.symbol);
    workflow->assignments[index].value = copy_text(assignment.value);
    workflow->assignments[index].source = assignment.source;
    if (workflow->assignments[index].symbol == 0 ||
        workflow->assignments[index].value == 0) {
      status = CONFIT_ERR_INTERNAL;
      goto fail;
    }
  }
  if (workflow->architecture == 0 || workflow->kernconf == 0) {
    status = CONFIT_ERR_INTERNAL;
    goto fail;
  }
  status = evaluate_assignments(workflow, diagnostic);
  if (status != CONFIT_OK) goto fail;
  *out_workflow = workflow;
  return CONFIT_OK;
fail:
  confit_v5_workflow_free(workflow);
  return status;
}

void confit_v5_workflow_free(ConfitV5Workflow *workflow) {
  if (workflow == 0) return;
  for (size_t index = 0U; index < workflow->assignment_count; ++index) {
    free(workflow->assignments[index].symbol);
    free(workflow->assignments[index].value);
  }
  free(workflow->assignments);
  free(workflow->architecture);
  free(workflow->kernconf);
  confit_v5_evaluation_free(workflow->evaluation);
  confit_v5_catalog_free(workflow->catalog);
  free(workflow);
}

size_t confit_v5_workflow_row_count(const ConfitV5Workflow *workflow) {
  return workflow != 0 ? confit_v5_catalog_option_count(workflow->catalog) : 0U;
}

int confit_v5_workflow_row(const ConfitV5Workflow *workflow, size_t index,
                           ConfitV5WorkflowRow *out_row) {
  if (workflow == 0 || out_row == 0 ||
      !workflow_option_at(workflow, index, &out_row->option))
    return 0;
  out_row->value = confit_v5_evaluation_value(workflow->evaluation,
                                               out_row->option.symbol);
  out_row->origin = assignment_index(workflow, out_row->option.symbol) >= 0
                        ? CONFIT_V5_VALUE_ORIGIN_USER
                        : CONFIT_V5_VALUE_ORIGIN_DEFAULT;
  out_row->available = 1;
  for (size_t edge = 0U; edge < out_row->option.prerequisite_count; ++edge)
    if (!value_enabled(workflow, out_row->option.prerequisites[edge]))
      out_row->available = 0;
  for (size_t edge = 0U; edge < out_row->option.visible_count; ++edge)
    if (!value_enabled(workflow, out_row->option.visible_all[edge]))
      out_row->available = 0;
  return out_row->value != 0;
}

static int contains_folded(const char *text, const char *query) {
  size_t query_size = strlen(query);
  if (query_size == 0U) return 1;
  for (size_t offset = 0U; text[offset] != '\0'; ++offset) {
    size_t match = 0U;
    while (match < query_size && text[offset + match] != '\0' &&
           tolower((unsigned char)text[offset + match]) ==
               tolower((unsigned char)query[match]))
      ++match;
    if (match == query_size) return 1;
  }
  return 0;
}

int confit_v5_workflow_search(const ConfitV5Workflow *workflow,
                              const char *query, size_t match_index,
                              size_t *out_row_index) {
  size_t query_size;
  size_t found = 0U;
  if (workflow == 0 || query == 0 || out_row_index == 0) return 0;
  query_size = strlen(query);
  if (query_size > CONFIT_V5_WORKFLOW_QUERY_MAX) return 0;
  for (size_t index = 0U; index < confit_v5_workflow_row_count(workflow);
       ++index) {
    ConfitV5WorkflowRow row;
    int matched = 0;
    if (!confit_v5_workflow_row(workflow, index, &row)) return 0;
    matched = contains_folded(row.option.symbol, query) ||
              contains_folded(row.option.prompt, query) ||
              contains_folded(row.option.help, query);
    for (size_t tag = 0U; !matched && tag < row.option.tag_count; ++tag)
      matched = contains_folded(row.option.tags[tag], query);
    if (matched && found++ == match_index) {
      *out_row_index = index;
      return 1;
    }
  }
  return 0;
}

ConfitStatus confit_v5_workflow_set(ConfitV5Workflow *workflow,
                                    const char *symbol, const char *value,
                                    ConfitDiagnostic *diagnostic) {
  int index;
  char *copy;
  ConfitStatus status;
  if (workflow == 0 || symbol == 0 || value == 0)
    return CONFIT_ERR_INVALID_ARGUMENT;
  if (!confit_v5_catalog_option(workflow->catalog, symbol,
                                &(ConfitV5OptionView){0})) {
    set_diag(diagnostic, CONFIT_ERR_SCHEMA, symbol,
             "workflow assignment references an unknown option");
    return CONFIT_ERR_SCHEMA;
  }
  copy = copy_text(value);
  if (copy == 0) return CONFIT_ERR_INTERNAL;
  index = assignment_index(workflow, symbol);
  if (index < 0) {
    WorkflowAssignment *grown;
    if (workflow->assignment_count >= CONFIT_WORKFLOW_MAX_ASSIGNMENTS) {
      free(copy);
      return CONFIT_ERR_SCHEMA;
    }
    grown = (WorkflowAssignment *)realloc(
        workflow->assignments,
        (workflow->assignment_count + 1U) * sizeof(workflow->assignments[0]));
    if (grown == 0) {
      free(copy);
      return CONFIT_ERR_INTERNAL;
    }
    workflow->assignments = grown;
    index = (int)workflow->assignment_count++;
    memset(&workflow->assignments[index], 0,
           sizeof(workflow->assignments[index]));
    workflow->assignments[index].symbol = copy_text(symbol);
    workflow->assignments[index].source.path = "<workflow>";
    workflow->assignments[index].source.line = 1U;
    workflow->assignments[index].source.column = 1U;
    if (workflow->assignments[index].symbol == 0) {
      free(copy);
      return CONFIT_ERR_INTERNAL;
    }
  }
  free(workflow->assignments[index].value);
  workflow->assignments[index].value = copy;
  status = evaluate_assignments(workflow, diagnostic);
  return status;
}

ConfitStatus confit_v5_workflow_minimal(const ConfitV5Workflow *workflow,
                                        char **out_text, size_t *out_size,
                                        ConfitDiagnostic *diagnostic) {
  WorkflowText text = {0};
  ConfitStatus status;
  if (workflow == 0 || out_text == 0) return CONFIT_ERR_INVALID_ARGUMENT;
  *out_text = 0;
  if (out_size != 0) *out_size = 0U;
  status = text_add(&text, "schema_version = 5\n");
  for (size_t index = 0U; status == CONFIT_OK &&
                           index < confit_v5_workflow_row_count(workflow);
       ++index) {
    ConfitV5WorkflowRow row;
    if (!confit_v5_workflow_row(workflow, index, &row)) {
      status = CONFIT_ERR_INTERNAL;
      break;
    }
    if (strcmp(row.value, row.option.default_value) == 0) continue;
    status = text_add(&text, "\n[[assignments]]\nsymbol = ");
    if (status == CONFIT_OK) status = text_quote(&text, row.option.symbol);
    if (status == CONFIT_OK) status = text_add(&text, "\nvalue = ");
    if (status == CONFIT_OK) status = text_quote(&text, row.value);
    if (status == CONFIT_OK) status = text_add(&text, "\n");
  }
  if (status != CONFIT_OK) {
    free(text.bytes);
    set_diag(diagnostic, status, workflow->kernconf,
             "minimal KERNCONF serialization exceeded its bound");
    return status;
  }
  *out_text = text.bytes;
  if (out_size != 0) *out_size = text.size;
  return CONFIT_OK;
}

static const char *origin_name(ConfitV5ValueOrigin origin) {
  if (origin == CONFIT_V5_VALUE_ORIGIN_USER) return "user";
  if (origin == CONFIT_V5_VALUE_ORIGIN_DERIVED) return "derived";
  return "default";
}

static const char *reason_name(ConfitV5ReasonKind kind) {
  static const char *const names[] = {"invalid", "default", "request",
                                     "prerequisite", "visibility", "choice",
                                     "rule", "cycle", "ambiguity"};
  return (size_t)kind < sizeof(names) / sizeof(names[0]) ? names[kind]
                                                         : "invalid";
}

ConfitStatus confit_v5_workflow_explain(const ConfitV5Workflow *workflow,
                                        const char *symbol, int only_blockers,
                                        char **out_text, size_t *out_size,
                                        ConfitDiagnostic *diagnostic) {
  WorkflowText text = {0};
  ConfitV5OptionView option;
  ConfitV5WorkflowRow row;
  size_t row_index = 0U;
  ConfitStatus status = CONFIT_OK;
  if (workflow == 0 || symbol == 0 || out_text == 0)
    return CONFIT_ERR_INVALID_ARGUMENT;
  *out_text = 0;
  if (out_size != 0) *out_size = 0U;
  if (!confit_v5_catalog_option(workflow->catalog, symbol, &option)) {
    set_diag(diagnostic, CONFIT_ERR_SCHEMA, symbol, "unknown Config v5 option");
    return CONFIT_ERR_SCHEMA;
  }
  while (row_index < confit_v5_workflow_row_count(workflow)) {
    if (confit_v5_workflow_row(workflow, row_index, &row) &&
        strcmp(row.option.symbol, symbol) == 0)
      break;
    ++row_index;
  }
  if (row_index == confit_v5_workflow_row_count(workflow))
    return CONFIT_ERR_INTERNAL;
  status = text_printf(&text,
                       "symbol=%s\nprompt=%s\nvalue=%s\norigin=%s\n"
                       "available=%s\nmenu=%s\nowner=%s\nsince=%s\n"
                       "stability=%s\nhelp=%s\nsource=%s:%zu:%zu\n",
                       option.symbol, option.prompt, row.value,
                       origin_name(row.origin), row.available ? "yes" : "no",
                       option.menu, option.owner, option.since, option.stability,
                       option.help, option.declaration.path,
                       option.declaration.line, option.declaration.column);
  for (size_t index = 0U; status == CONFIT_OK &&
                           index < confit_v5_evaluation_reason_count(
                                       workflow->evaluation);
       ++index) {
    ConfitV5ReasonView reason;
    if (!confit_v5_evaluation_reason(workflow->evaluation, index, &reason)) {
      status = CONFIT_ERR_INTERNAL;
      break;
    }
    if (strcmp(reason.subject, symbol) != 0 ||
        (only_blockers && reason.satisfied))
      continue;
    status = text_printf(&text, "reason=%s:%s:%s@%s:%zu:%zu\n",
                         reason_name(reason.kind),
                         reason.satisfied ? "satisfied" : "blocked",
                         reason.cause, reason.source.path, reason.source.line,
                         reason.source.column);
  }
  if (only_blockers && row.available)
    status = text_add(&text, "reason=available:no-blocker\n");
  if (status != CONFIT_OK) {
    free(text.bytes);
    return status;
  }
  *out_text = text.bytes;
  if (out_size != 0) *out_size = text.size;
  return CONFIT_OK;
}

ConfitStatus confit_v5_workflow_render(const ConfitV5Workflow *workflow,
                                       size_t selected_row,
                                       const char *query, size_t width,
                                       size_t height, char **out_text,
                                       size_t *out_size,
                                       ConfitDiagnostic *diagnostic) {
  WorkflowText text = {0};
  ConfitV5WorkflowRow selected;
  ConfitStatus status;
  size_t visible_rows;
  size_t emitted = 0U;
  if (workflow == 0 || out_text == 0 || query == 0 ||
      strlen(query) > CONFIT_V5_WORKFLOW_QUERY_MAX || width < 40U ||
      width > 512U || height < 10U || height > 256U ||
      selected_row >= confit_v5_workflow_row_count(workflow)) {
    set_diag(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, "<tui>",
             "terminal dimensions, query, or selected row are out of bounds");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_text = 0;
  if (out_size != 0) *out_size = 0U;
  if (!confit_v5_workflow_row(workflow, selected_row, &selected))
    return CONFIT_ERR_INTERNAL;
  status = text_printf(&text,
                       "Confit Config v5  ARCH=%s  KERNCONF=%s\n"
                       "Search: %s\n%-24s %-24s %-10s %s\n",
                       workflow->architecture, workflow->kernconf, query,
                       "Menu", "Option", "Origin", "Value");
  visible_rows = height - 8U;
  for (size_t match = 0U; status == CONFIT_OK && emitted < visible_rows;
       ++match) {
    size_t row_index;
    ConfitV5WorkflowRow row;
    if (!confit_v5_workflow_search(workflow, query, match, &row_index)) break;
    if (!confit_v5_workflow_row(workflow, row_index, &row)) {
      status = CONFIT_ERR_INTERNAL;
      break;
    }
    status = text_printf(&text, "%c %-22.22s %-24.24s %-10s %s%s\n",
                         row_index == selected_row ? '>' : ' ', row.option.menu,
                         row.option.prompt, origin_name(row.origin), row.value,
                         row.available ? "" : " [unavailable]");
    ++emitted;
  }
  if (status == CONFIT_OK)
    status = text_printf(&text,
                         "\nDetail: %s\n%s\nowner=%s  since=%s  stability=%s\n"
                         "Keys: arrows/jk move  /=search  ?=help  p=preview  "
                         "a=apply  c=cancel  q=quit\n",
                         selected.option.symbol, selected.option.help,
                         selected.option.owner, selected.option.since,
                         selected.option.stability);
  if (status != CONFIT_OK) {
    free(text.bytes);
    return status;
  }
  *out_text = text.bytes;
  if (out_size != 0) *out_size = text.size;
  return CONFIT_OK;
}

ConfitStatus confit_v5_tui_decode(const char *input,
                                  ConfitV5TuiAction *out_action,
                                  ConfitDiagnostic *diagnostic) {
  size_t size;
  if (input == 0 || out_action == 0) return CONFIT_ERR_INVALID_ARGUMENT;
  memset(out_action, 0, sizeof(*out_action));
  size = strlen(input);
  if (size > 511U) goto invalid;
  for (size_t index = 0U; index < size; ++index) {
    unsigned char byte = (unsigned char)input[index];
    if ((byte < 0x20U || byte == 0x7fU) &&
        !(index == 0U && byte == 0x1bU))
      goto invalid;
  }
  if (size == 0U) out_action->kind = CONFIT_V5_TUI_ACTION_NONE;
  else if (strcmp(input, "j") == 0 || strcmp(input, "\033[B") == 0)
    out_action->kind = CONFIT_V5_TUI_ACTION_DOWN;
  else if (strcmp(input, "k") == 0 || strcmp(input, "\033[A") == 0)
    out_action->kind = CONFIT_V5_TUI_ACTION_UP;
  else if (strcmp(input, "q") == 0 || strcmp(input, "c") == 0 ||
           strcmp(input, "cancel") == 0 || strcmp(input, "\033") == 0)
    out_action->kind = CONFIT_V5_TUI_ACTION_CANCEL;
  else if (strcmp(input, "p") == 0 || strcmp(input, "preview") == 0)
    out_action->kind = CONFIT_V5_TUI_ACTION_PREVIEW;
  else if (strcmp(input, "a") == 0 || strcmp(input, "apply") == 0)
    out_action->kind = CONFIT_V5_TUI_ACTION_APPLY;
  else if (strcmp(input, "?") == 0 || strcmp(input, "help") == 0)
    out_action->kind = CONFIT_V5_TUI_ACTION_HELP;
  else if (input[0] == '/') {
    if (size - 1U > CONFIT_V5_WORKFLOW_QUERY_MAX) goto invalid;
    out_action->kind = CONFIT_V5_TUI_ACTION_SEARCH;
    memcpy(out_action->value, input + 1U, size);
  } else if (strncmp(input, "set ", 4U) == 0) {
    const char *separator = strchr(input + 4U, ' ');
    size_t symbol_size;
    size_t value_size;
    if (separator == 0 || separator == input + 4U || separator[1] == '\0')
      goto invalid;
    symbol_size = (size_t)(separator - (input + 4U));
    value_size = strlen(separator + 1U);
    if (symbol_size >= sizeof(out_action->symbol) ||
        value_size >= sizeof(out_action->value))
      goto invalid;
    for (size_t index = 0U; index < symbol_size; ++index) {
      unsigned char byte = (unsigned char)input[4U + index];
      if (!(byte == '_' || (byte >= 'A' && byte <= 'Z') ||
            (index != 0U && byte >= '0' && byte <= '9')))
        goto invalid;
    }
    out_action->kind = CONFIT_V5_TUI_ACTION_SET;
    memcpy(out_action->symbol, input + 4U, symbol_size);
    out_action->symbol[symbol_size] = '\0';
    memcpy(out_action->value, separator + 1U, value_size + 1U);
  } else {
    goto invalid;
  }
  return CONFIT_OK;
invalid:
  set_diag(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, "<tui-input>",
           "TUI input is malformed, contains control bytes, or exceeds bounds");
  return CONFIT_ERR_INVALID_ARGUMENT;
}
