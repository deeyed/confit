#include "confit/tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "confit/graph.h"
#include "confit/model.h"
#include "confit/resolver.h"
#include "confit/schema.h"
#include "confit_tui.h"

typedef struct ConfitTuiRow {
  CftuiListItem item;
  char detail[192];
  char value[96];
} ConfitTuiRow;

static const char *confit_tui_text_or_dash(const char *text) {
  return text != 0 && text[0] != '\0' ? text : "-";
}

static const char *confit_tui_effective_target_name(
    const ConfitProject *project, const char *profile_name,
    const char *target_name) {
  size_t index;

  if (target_name != 0) {
    return target_name;
  }
  if (project == 0 || profile_name == 0) {
    return 0;
  }
  for (index = 0U; index < project->profile_count; ++index) {
    const ConfitProfile *profile = &project->profiles[index];
    if (profile->name != 0 && strcmp(profile->name, profile_name) == 0) {
      return profile->target;
    }
  }
  return 0;
}

static void confit_tui_format_value(const ConfitOption *option,
                                    const ConfitValue *value, char *out,
                                    size_t out_size) {
  if (out == 0 || out_size == 0U) {
    return;
  }

  switch (value != 0 ? value->kind : CONFIT_VALUE_EMPTY) {
  case CONFIT_VALUE_BOOL:
    (void)snprintf(out, out_size, "%s",
                   value->as.bool_value ? "true" : "false");
    break;
  case CONFIT_VALUE_INT:
    (void)snprintf(out, out_size, "%lld", (long long)value->as.int_value);
    break;
  case CONFIT_VALUE_UINT:
    if (option != 0 && option->type == CONFIT_OPTION_TYPE_HEX) {
      (void)snprintf(out, out_size, "0x%llX",
                     (unsigned long long)value->as.uint_value);
    } else {
      (void)snprintf(out, out_size, "%llu",
                     (unsigned long long)value->as.uint_value);
    }
    break;
  case CONFIT_VALUE_FLOAT:
    (void)snprintf(out, out_size, "%.6g", value->as.float_value);
    break;
  case CONFIT_VALUE_STRING:
  case CONFIT_VALUE_ENUM:
  case CONFIT_VALUE_PATH:
    (void)snprintf(out, out_size, "%s",
                   confit_tui_text_or_dash(value->as.string_value));
    break;
  case CONFIT_VALUE_EMPTY:
  default:
    (void)snprintf(out, out_size, "-");
    break;
  }
  out[out_size - 1U] = '\0';
}

static ConfitStatus confit_tui_make_rows(const ConfitProject *project,
                                         const ConfitResolvedConfig *config,
                                         ConfitTuiRow **out_rows,
                                         size_t *out_count,
                                         ConfitDiagnostic *diagnostic) {
  ConfitTuiRow *rows;
  size_t index;

  if (out_rows == 0 || out_count == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "missing tui row output");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  *out_rows = 0;
  *out_count = 0U;
  if (project->option_count == 0U) {
    return CONFIT_OK;
  }

  rows = (ConfitTuiRow *)calloc(project->option_count, sizeof(rows[0]));
  if (rows == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INTERNAL, 0, 0, 0,
                          "failed to allocate tui rows");
    return CONFIT_ERR_INTERNAL;
  }

  for (index = 0U; index < project->option_count; ++index) {
    const ConfitOption *option = &project->options[index];
    const ConfitResolvedValue *resolved =
        confit_resolved_config_find(config, option->id);

    (void)snprintf(rows[index].detail, sizeof(rows[index].detail),
                   "%s | %s | %s",
                   confit_option_type_name(option->type),
                   confit_tui_text_or_dash(option->category),
                   confit_tui_text_or_dash(option->prompt));
    rows[index].detail[sizeof(rows[index].detail) - 1U] = '\0';

    confit_tui_format_value(
        option,
        resolved != 0 ? &resolved->value : &option->default_value,
        rows[index].value, sizeof(rows[index].value));
    rows[index].item.label = option->id;
    rows[index].item.detail = rows[index].detail;
    rows[index].item.value = rows[index].value;
  }

  *out_rows = rows;
  *out_count = project->option_count;
  return CONFIT_OK;
}

static ConfitStatus confit_tui_load_checked_project(
    const ConfitTuiOptions *options, ConfitProject **out_project,
    ConfitGraph **out_graph, ConfitResolvedConfig **out_config,
    ConfitDiagnostic *diagnostic) {
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitResolvedConfig *config;
  ConfitStatus status;

  project = 0;
  graph = 0;
  config = 0;
  *out_project = 0;
  *out_graph = 0;
  *out_config = 0;

  status = confit_schema_load_project(options->project_root, &project,
                                      diagnostic);
  if (status == CONFIT_OK) {
    status = confit_graph_build(project, &graph, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_graph_validate(graph, diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_resolver_resolve(project, options->profile_name,
                                     options->target_name, 0, 0U, &config,
                                     diagnostic);
  }
  if (status != CONFIT_OK) {
    confit_resolved_config_free(config);
    confit_graph_free(graph);
    confit_project_free(project);
    return status;
  }

  *out_project = project;
  *out_graph = graph;
  *out_config = config;
  return CONFIT_OK;
}

static ConfitStatus confit_tui_render_loop(const ConfitProject *project,
                                           const ConfitTuiOptions *options,
                                           const ConfitTuiRow *rows,
                                           size_t row_count,
                                           const char *target_name) {
  CftuiListItem *items;
  CftuiScreen screen;
  size_t selected;
  size_t index;
  char subtitle[256];
  char status_line[256];
  CftuiKey key;

  items = 0;
  if (row_count > 0U) {
    items = (CftuiListItem *)calloc(row_count, sizeof(items[0]));
    if (items == 0) {
      return CONFIT_ERR_INTERNAL;
    }
    for (index = 0U; index < row_count; ++index) {
      items[index] = rows[index].item;
    }
  }

  selected = 0U;
  key = CFTUI_KEY_NONE;
  (void)snprintf(subtitle, sizeof(subtitle),
                 "project=%s profile=%s target=%s",
                 confit_tui_text_or_dash(project->name),
                 confit_tui_text_or_dash(options->profile_name),
                 confit_tui_text_or_dash(target_name));
  subtitle[sizeof(subtitle) - 1U] = '\0';

  do {
    (void)snprintf(status_line, sizeof(status_line),
                   "option %lu/%lu | keys: j/down k/up q quit | read-only "
                   "shell",
                   row_count == 0U ? 0UL : (unsigned long)(selected + 1U),
                   (unsigned long)row_count);
    status_line[sizeof(status_line) - 1U] = '\0';

    screen.title = "Confit TUI";
    screen.subtitle = subtitle;
    screen.items = items;
    screen.item_count = row_count;
    screen.selected_index = selected;
    screen.status = status_line;
    if (cftui_render(&screen) != 0) {
      free(items);
      return CONFIT_ERR_INTERNAL;
    }

    key = cftui_read_key();
    if (key == CFTUI_KEY_DOWN && selected + 1U < row_count) {
      selected += 1U;
    } else if (key == CFTUI_KEY_UP && selected > 0U) {
      selected -= 1U;
    }
  } while (key != CFTUI_KEY_QUIT);

  free(items);
  return CONFIT_OK;
}

ConfitStatus confit_tui_run(const ConfitTuiOptions *options,
                            ConfitDiagnostic *diagnostic) {
  ConfitProject *project;
  ConfitGraph *graph;
  ConfitResolvedConfig *config;
  ConfitTuiRow *rows;
  size_t row_count;
  const char *target_name;
  ConfitStatus status;

  if (options == 0 || options->project_root == 0 ||
      options->profile_name == 0) {
    confit_diagnostic_set(diagnostic, CONFIT_ERR_INVALID_ARGUMENT, 0, 0, 0,
                          "invalid tui options");
    return CONFIT_ERR_INVALID_ARGUMENT;
  }

  project = 0;
  graph = 0;
  config = 0;
  rows = 0;
  row_count = 0U;
  status = confit_tui_load_checked_project(options, &project, &graph, &config,
                                           diagnostic);
  if (status == CONFIT_OK) {
    status = confit_tui_make_rows(project, config, &rows, &row_count,
                                  diagnostic);
  }
  if (status == CONFIT_OK) {
    target_name = confit_tui_effective_target_name(
        project, options->profile_name, options->target_name);
    status = confit_tui_render_loop(project, options, rows, row_count,
                                    target_name);
  }

  free(rows);
  confit_resolved_config_free(config);
  confit_graph_free(graph);
  confit_project_free(project);
  return status;
}
