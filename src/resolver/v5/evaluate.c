#include "../../schema/v5/config_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const ConfitV5EffectiveValue *confit_v5_find_effective(
    const ConfitV5Evaluation *evaluation, const char *symbol) {
  if (evaluation == 0 || symbol == 0) return 0;
  for (size_t index = 0U; index < evaluation->value_count; ++index) {
    if (strcmp(evaluation->values[index].symbol, symbol) == 0)
      return &evaluation->values[index];
  }
  return 0;
}

static ConfitV5EffectiveValue *confit_v5_find_effective_mutable(
    ConfitV5Evaluation *evaluation, const char *symbol) {
  return (ConfitV5EffectiveValue *)(void *)
      confit_v5_find_effective(evaluation, symbol);
}

int confit_v5_option_value_valid(const ConfitV5Option *option,
                                 const char *value, int *out_enabled) {
  if (option == 0 || value == 0 || out_enabled == 0) return 0;
  *out_enabled = 0;
  switch (option->type) {
  case CONFIT_V5_OPTION_BOOL:
    if (strcmp(value, "false") == 0) return 1;
    if (strcmp(value, "true") == 0) {
      *out_enabled = 1;
      return 1;
    }
    return 0;
  case CONFIT_V5_OPTION_PLACEMENT:
    for (size_t index = 0U; index < option->allowed.count; ++index) {
      if (strcmp(option->allowed.items[index], value) == 0) {
        *out_enabled = strcmp(value, "off") != 0;
        return 1;
      }
    }
    return 0;
  case CONFIT_V5_OPTION_ENUM:
    for (size_t index = 0U; index < option->values.count; ++index) {
      if (strcmp(option->values.items[index], value) == 0) {
        for (size_t enabled = 0U; enabled < option->enabled_values.count;
             ++enabled) {
          if (strcmp(option->enabled_values.items[enabled], value) == 0) {
            *out_enabled = 1;
            break;
          }
        }
        return 1;
      }
    }
    return 0;
  case CONFIT_V5_OPTION_INTEGER: {
    char *end = 0;
    long long parsed;
    if (value[0] == '\0' || value[0] == '+' ||
        (value[0] == '0' && value[1] != '\0') ||
        (value[0] == '-' && value[1] == '0' && value[2] != '\0')) return 0;
    errno = 0;
    parsed = strtoll(value, &end, 10);
    return errno == 0 && end != value && *end == '\0' &&
           parsed >= option->minimum && parsed <= option->maximum;
  }
  case CONFIT_V5_OPTION_STRING:
    if (strlen(value) <= CONFIT_V5_MAX_TEXT_BYTES &&
        strchr(value, '\n') == 0 && strchr(value, '\r') == 0) {
      *out_enabled = 1;
      return 1;
    }
    return 0;
  default:
    return 0;
  }
}

ConfitStatus confit_v5_evaluation_add_reason(
    ConfitV5Evaluation *evaluation, ConfitV5ReasonKind kind, int satisfied,
    const char *subject, const char *cause, const ConfitV5OwnedSpan *source) {
  ConfitV5Reason *grown;
  ConfitV5Reason reason;
  if (evaluation == 0 || subject == 0 || source == 0 || source->path == 0)
    return CONFIT_ERR_INTERNAL;
  memset(&reason, 0, sizeof(reason));
  reason.kind = kind;
  reason.satisfied = satisfied ? 1 : 0;
  reason.subject = confit_v5_copy(subject);
  reason.cause = confit_v5_copy(cause != 0 ? cause : "");
  if (reason.subject == 0 || reason.cause == 0 ||
      !confit_v5_owned_span_set(&reason.source, source->path, source->line,
                                source->column)) {
    free(reason.subject);
    free(reason.cause);
    confit_v5_owned_span_clear(&reason.source);
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV5Reason *)realloc(
      evaluation->reasons,
      (evaluation->reason_count + 1U) * sizeof(evaluation->reasons[0]));
  if (grown == 0) {
    free(reason.subject);
    free(reason.cause);
    confit_v5_owned_span_clear(&reason.source);
    return CONFIT_ERR_INTERNAL;
  }
  evaluation->reasons = grown;
  evaluation->reasons[evaluation->reason_count++] = reason;
  return CONFIT_OK;
}

static ConfitStatus confit_v5_initialize_values(
    const ConfitV5Catalog *catalog, ConfitV5Evaluation *evaluation) {
  evaluation->values = (ConfitV5EffectiveValue *)calloc(
      catalog->option_count, sizeof(evaluation->values[0]));
  if (evaluation->values == 0 && catalog->option_count != 0U)
    return CONFIT_ERR_INTERNAL;
  evaluation->value_count = catalog->option_count;
  for (size_t index = 0U; index < catalog->option_count; ++index) {
    const ConfitV5Option *option = &catalog->options[index];
    ConfitV5EffectiveValue *value = &evaluation->values[index];
    value->symbol = confit_v5_copy(option->symbol);
    value->value = confit_v5_copy(option->default_value);
    if (value->symbol == 0 || value->value == 0 ||
        !confit_v5_option_value_valid(option, value->value, &value->enabled) ||
        !confit_v5_owned_span_set(&value->source, option->default_source.path,
                                  option->default_source.line,
                                  option->default_source.column)) {
      return CONFIT_ERR_INTERNAL;
    }
    if (confit_v5_evaluation_add_reason(
            evaluation, CONFIT_V5_REASON_DEFAULT, 1, option->symbol,
            option->default_value, &option->default_source) != CONFIT_OK) {
      return CONFIT_ERR_INTERNAL;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v5_apply_assignments(
    const ConfitV5Catalog *catalog, const ConfitV5Assignment *assignments,
    size_t assignment_count, ConfitV5Evaluation *evaluation,
    ConfitDiagnostic *diagnostic) {
  if (assignment_count > CONFIT_V5_MAX_ASSIGNMENTS ||
      (assignment_count != 0U && assignments == 0))
    return CONFIT_ERR_INVALID_ARGUMENT;
  for (size_t index = 0U; index < assignment_count; ++index) {
    const ConfitV5Assignment *assignment = &assignments[index];
    const ConfitV5Option *option =
        confit_v5_find_option(catalog, assignment->symbol);
    ConfitV5EffectiveValue *value;
    ConfitV5OwnedSpan source;
    int enabled;
    memset(&source, 0, sizeof(source));
    source.path = (char *)(assignment->source.path != 0
                              ? assignment->source.path
                              : "confit://request");
    source.line = assignment->source.line != 0U ? assignment->source.line : 1U;
    source.column =
        assignment->source.column != 0U ? assignment->source.column : 1U;
    if (option == 0 || assignment->value == 0 ||
        !confit_v5_option_value_valid(option, assignment->value, &enabled)) {
      confit_v5_set_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, &source,
          option == 0 ? "assignment references an unknown Config v5 option"
                      : "assignment value is outside the option domain");
      return CONFIT_ERR_SCHEMA;
    }
    value = confit_v5_find_effective_mutable(evaluation, assignment->symbol);
    if (value == 0) return CONFIT_ERR_INTERNAL;
    for (size_t other = 0U; other < index; ++other) {
      if (strcmp(assignments[other].symbol, assignment->symbol) == 0) {
        confit_v5_set_diagnostic(
            diagnostic, CONFIT_ERR_CONFLICT, &source,
            "Config v5 forbids multiple writers for one symbol");
        return CONFIT_ERR_CONFLICT;
      }
    }
    free(value->value);
    value->value = confit_v5_copy(assignment->value);
    value->enabled = enabled;
    if (value->value == 0 ||
        !confit_v5_owned_span_set(&value->source, source.path, source.line,
                                  source.column)) return CONFIT_ERR_INTERNAL;
    if (confit_v5_evaluation_add_reason(
            evaluation, CONFIT_V5_REASON_REQUEST, 1, option->symbol,
            assignment->value, &source) != CONFIT_OK) return CONFIT_ERR_INTERNAL;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v5_evaluate_prerequisites(
    const ConfitV5Catalog *catalog, ConfitV5Evaluation *evaluation,
    ConfitDiagnostic *diagnostic) {
  for (size_t index = 0U; index < catalog->option_count; ++index) {
    const ConfitV5Option *option = &catalog->options[index];
    const ConfitV5EffectiveValue *root =
        confit_v5_find_effective(evaluation, option->symbol);
    if (root == 0) return CONFIT_ERR_INTERNAL;
    for (size_t edge = 0U; edge < option->prerequisites.count; ++edge) {
      const ConfitV5EffectiveValue *required = confit_v5_find_effective(
          evaluation, option->prerequisites.items[edge]);
      const int satisfied = !root->enabled ||
                            (required != 0 && required->enabled);
      ConfitStatus status = confit_v5_evaluation_add_reason(
          evaluation, CONFIT_V5_REASON_PREREQUISITE, satisfied,
          option->symbol, option->prerequisites.items[edge],
          &option->prerequisites.spans[edge]);
      if (status != CONFIT_OK) return status;
      if (!satisfied) {
        confit_v5_set_diagnostic(
            diagnostic, CONFIT_ERR_DEPENDENCY,
            &option->prerequisites.spans[edge],
            "enabled option has a false constraints.all prerequisite");
        return CONFIT_ERR_DEPENDENCY;
      }
    }
    for (size_t edge = 0U; edge < option->visible_all.count; ++edge) {
      const ConfitV5EffectiveValue *required =
          confit_v5_find_effective(evaluation, option->visible_all.items[edge]);
      const int visible = required != 0 && required->enabled;
      ConfitStatus status = confit_v5_evaluation_add_reason(
          evaluation, CONFIT_V5_REASON_VISIBILITY, visible, option->symbol,
          option->visible_all.items[edge], &option->visible_all.spans[edge]);
      if (status != CONFIT_OK) return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v5_evaluate_choices(
    const ConfitV5Catalog *catalog, ConfitV5Evaluation *evaluation,
    ConfitDiagnostic *diagnostic) {
  for (size_t index = 0U; index < catalog->choice_count; ++index) {
    const ConfitV5Choice *choice = &catalog->choices[index];
    size_t enabled_count = 0U;
    char cause[64];
    for (size_t member = 0U; member < choice->members.count; ++member) {
      const ConfitV5EffectiveValue *value = confit_v5_find_effective(
          evaluation, choice->members.items[member]);
      if (value != 0 && value->enabled) ++enabled_count;
    }
    const int satisfied =
        choice->cardinality == CONFIT_V5_CHOICE_AT_MOST_ONE
            ? enabled_count <= 1U
            : enabled_count == 1U;
    (void)snprintf(cause, sizeof(cause), "enabled=%zu", enabled_count);
    if (confit_v5_evaluation_add_reason(
            evaluation, CONFIT_V5_REASON_CHOICE, satisfied, choice->symbol,
            cause, &choice->source) != CONFIT_OK) return CONFIT_ERR_INTERNAL;
    if (!satisfied) {
      confit_v5_set_diagnostic(diagnostic, CONFIT_ERR_CONFLICT,
                               &choice->source,
                               "choice cardinality is not satisfied");
      return CONFIT_ERR_CONFLICT;
    }
  }
  return CONFIT_OK;
}

static int confit_v5_all_enabled(const ConfitV5Evaluation *evaluation,
                                 const ConfitV5StringList *symbols) {
  for (size_t index = 0U; index < symbols->count; ++index) {
    const ConfitV5EffectiveValue *value =
        confit_v5_find_effective(evaluation, symbols->items[index]);
    if (value == 0 || !value->enabled) return 0;
  }
  return 1;
}

static ConfitStatus confit_v5_evaluate_rules(
    const ConfitV5Catalog *catalog, ConfitV5Evaluation *evaluation,
    ConfitDiagnostic *diagnostic) {
  for (size_t index = 0U; index < catalog->rule_count; ++index) {
    const ConfitV5Rule *rule = &catalog->rules[index];
    const int applies = confit_v5_all_enabled(evaluation, &rule->if_all);
    const int satisfied = !applies ||
                          confit_v5_all_enabled(evaluation,
                                                &rule->require_all);
    if (confit_v5_evaluation_add_reason(
            evaluation, CONFIT_V5_REASON_RULE, satisfied, rule->message,
            applies ? "if_all=true" : "if_all=false", &rule->source) !=
        CONFIT_OK) return CONFIT_ERR_INTERNAL;
    if (!satisfied) {
      confit_v5_set_diagnostic(diagnostic, CONFIT_ERR_CONFLICT, &rule->source,
                               rule->message);
      return CONFIT_ERR_CONFLICT;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v5_evaluate_internal(
    const ConfitV5Catalog *catalog, const ConfitV5Assignment *assignments,
    size_t assignment_count, ConfitV5Evaluation **out_evaluation,
    ConfitDiagnostic *diagnostic) {
  ConfitV5Evaluation *evaluation;
  ConfitStatus status;
  if (catalog == 0 || out_evaluation == 0) return CONFIT_ERR_INVALID_ARGUMENT;
  *out_evaluation = 0;
  evaluation = (ConfitV5Evaluation *)calloc(1U, sizeof(*evaluation));
  if (evaluation == 0) return CONFIT_ERR_INTERNAL;
  status = confit_v5_initialize_values(catalog, evaluation);
  if (status == CONFIT_OK)
    status = confit_v5_apply_assignments(catalog, assignments,
                                         assignment_count, evaluation,
                                         diagnostic);
  if (status == CONFIT_OK)
    status = confit_v5_evaluate_prerequisites(catalog, evaluation, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v5_evaluate_choices(catalog, evaluation, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v5_evaluate_rules(catalog, evaluation, diagnostic);
  *out_evaluation = evaluation;
  return status;
}

ConfitStatus confit_v5_evaluate(
    const ConfitV5Catalog *catalog, const ConfitV5Assignment *assignments,
    size_t assignment_count, ConfitV5Evaluation **out_evaluation,
    ConfitDiagnostic *diagnostic) {
  return confit_v5_evaluate_internal(
      catalog, assignments, assignment_count, out_evaluation, diagnostic);
}

ConfitStatus confit_v5_evaluate_kernconf(
    const ConfitV5Catalog *catalog, ConfitV5Evaluation **out_evaluation,
    ConfitDiagnostic *diagnostic) {
  ConfitV5Assignment *assignments = 0;
  ConfitStatus status;
  if (catalog == 0 || out_evaluation == 0)
    return CONFIT_ERR_INVALID_ARGUMENT;
  if (catalog->assignment_count != 0U) {
    assignments = (ConfitV5Assignment *)calloc(
        catalog->assignment_count, sizeof(assignments[0]));
    if (assignments == 0) return CONFIT_ERR_INTERNAL;
  }
  for (size_t index = 0U; index < catalog->assignment_count; ++index) {
    assignments[index].symbol = catalog->assignments[index].symbol;
    assignments[index].value = catalog->assignments[index].value;
    assignments[index].source.path = catalog->assignments[index].source.path;
    assignments[index].source.line = catalog->assignments[index].source.line;
    assignments[index].source.column =
        catalog->assignments[index].source.column;
  }
  status = confit_v5_evaluate(catalog, assignments,
                              catalog->assignment_count, out_evaluation,
                              diagnostic);
  free(assignments);
  return status;
}

void confit_v5_evaluation_free(ConfitV5Evaluation *evaluation) {
  if (evaluation == 0) return;
  for (size_t index = 0U; index < evaluation->value_count; ++index) {
    free(evaluation->values[index].symbol);
    free(evaluation->values[index].value);
    confit_v5_owned_span_clear(&evaluation->values[index].source);
  }
  free(evaluation->values);
  for (size_t index = 0U; index < evaluation->reason_count; ++index) {
    free(evaluation->reasons[index].subject);
    free(evaluation->reasons[index].cause);
    confit_v5_owned_span_clear(&evaluation->reasons[index].source);
  }
  free(evaluation->reasons);
  free(evaluation);
}

const char *confit_v5_evaluation_value(const ConfitV5Evaluation *evaluation,
                                       const char *symbol) {
  const ConfitV5EffectiveValue *value =
      confit_v5_find_effective(evaluation, symbol);
  return value != 0 ? value->value : 0;
}

size_t confit_v5_evaluation_value_count(
    const ConfitV5Evaluation *evaluation) {
  return evaluation != 0 ? evaluation->value_count : 0U;
}

int confit_v5_evaluation_value_at(
    const ConfitV5Evaluation *evaluation, size_t index, const char **out_symbol,
    const char **out_value, int *out_enabled, ConfitV5SourceSpan *out_source) {
  const ConfitV5EffectiveValue *value;
  if (evaluation == 0 || index >= evaluation->value_count || out_symbol == 0 ||
      out_value == 0 || out_enabled == 0 || out_source == 0)
    return 0;
  value = &evaluation->values[index];
  *out_symbol = value->symbol;
  *out_value = value->value;
  *out_enabled = value->enabled;
  out_source->path = value->source.path;
  out_source->line = value->source.line;
  out_source->column = value->source.column;
  return 1;
}

size_t confit_v5_evaluation_reason_count(
    const ConfitV5Evaluation *evaluation) {
  return evaluation != 0 ? evaluation->reason_count : 0U;
}

int confit_v5_evaluation_reason(const ConfitV5Evaluation *evaluation,
                                size_t index,
                                ConfitV5ReasonView *out_reason) {
  const ConfitV5Reason *reason;
  if (evaluation == 0 || out_reason == 0 ||
      index >= evaluation->reason_count) return 0;
  reason = &evaluation->reasons[index];
  out_reason->kind = reason->kind;
  out_reason->satisfied = reason->satisfied;
  out_reason->subject = reason->subject;
  out_reason->cause = reason->cause;
  out_reason->source.path = reason->source.path;
  out_reason->source.line = reason->source.line;
  out_reason->source.column = reason->source.column;
  return 1;
}
