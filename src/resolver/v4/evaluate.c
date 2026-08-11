#include "../../schema/v4/config_internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const ConfitV4EffectiveValue *confit_v4_find_effective(
    const ConfitV4Evaluation *evaluation, const char *symbol) {
  if (evaluation == 0 || symbol == 0) return 0;
  for (size_t index = 0U; index < evaluation->value_count; ++index) {
    if (strcmp(evaluation->values[index].symbol, symbol) == 0)
      return &evaluation->values[index];
  }
  return 0;
}

static ConfitV4EffectiveValue *confit_v4_find_effective_mutable(
    ConfitV4Evaluation *evaluation, const char *symbol) {
  return (ConfitV4EffectiveValue *)(void *)
      confit_v4_find_effective(evaluation, symbol);
}

int confit_v4_option_value_valid(const ConfitV4Option *option,
                                 const char *value, int *out_enabled) {
  if (option == 0 || value == 0 || out_enabled == 0) return 0;
  *out_enabled = 0;
  switch (option->type) {
  case CONFIT_V4_OPTION_BOOL:
    if (strcmp(value, "false") == 0) return 1;
    if (strcmp(value, "true") == 0) {
      *out_enabled = 1;
      return 1;
    }
    return 0;
  case CONFIT_V4_OPTION_PLACEMENT:
    for (size_t index = 0U; index < option->allowed.count; ++index) {
      if (strcmp(option->allowed.items[index], value) == 0) {
        *out_enabled = strcmp(value, "off") != 0;
        return 1;
      }
    }
    return 0;
  case CONFIT_V4_OPTION_ENUM:
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
  case CONFIT_V4_OPTION_INTEGER: {
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
  case CONFIT_V4_OPTION_STRING:
    return strlen(value) <= CONFIT_V4_MAX_TEXT_BYTES &&
           strchr(value, '\n') == 0 && strchr(value, '\r') == 0;
  default:
    return 0;
  }
}

ConfitStatus confit_v4_evaluation_add_reason(
    ConfitV4Evaluation *evaluation, ConfitV4ReasonKind kind, int satisfied,
    const char *subject, const char *cause, const ConfitV4OwnedSpan *source) {
  ConfitV4Reason *grown;
  ConfitV4Reason reason;
  if (evaluation == 0 || subject == 0 || source == 0 || source->path == 0)
    return CONFIT_ERR_INTERNAL;
  memset(&reason, 0, sizeof(reason));
  reason.kind = kind;
  reason.satisfied = satisfied ? 1 : 0;
  reason.subject = confit_v4_copy(subject);
  reason.cause = confit_v4_copy(cause != 0 ? cause : "");
  if (reason.subject == 0 || reason.cause == 0 ||
      !confit_v4_owned_span_set(&reason.source, source->path, source->line,
                                source->column)) {
    free(reason.subject);
    free(reason.cause);
    confit_v4_owned_span_clear(&reason.source);
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV4Reason *)realloc(
      evaluation->reasons,
      (evaluation->reason_count + 1U) * sizeof(evaluation->reasons[0]));
  if (grown == 0) {
    free(reason.subject);
    free(reason.cause);
    confit_v4_owned_span_clear(&reason.source);
    return CONFIT_ERR_INTERNAL;
  }
  evaluation->reasons = grown;
  evaluation->reasons[evaluation->reason_count++] = reason;
  return CONFIT_OK;
}

static ConfitStatus confit_v4_initialize_values(
    const ConfitV4Catalog *catalog, ConfitV4Evaluation *evaluation) {
  evaluation->values = (ConfitV4EffectiveValue *)calloc(
      catalog->option_count, sizeof(evaluation->values[0]));
  if (evaluation->values == 0 && catalog->option_count != 0U)
    return CONFIT_ERR_INTERNAL;
  evaluation->value_count = catalog->option_count;
  for (size_t index = 0U; index < catalog->option_count; ++index) {
    const ConfitV4Option *option = &catalog->options[index];
    ConfitV4EffectiveValue *value = &evaluation->values[index];
    value->symbol = confit_v4_copy(option->symbol);
    value->value = confit_v4_copy(option->default_value);
    if (value->symbol == 0 || value->value == 0 ||
        !confit_v4_option_value_valid(option, value->value, &value->enabled) ||
        !confit_v4_owned_span_set(&value->source, option->default_source.path,
                                  option->default_source.line,
                                  option->default_source.column)) {
      return CONFIT_ERR_INTERNAL;
    }
    if (confit_v4_evaluation_add_reason(
            evaluation, CONFIT_V4_REASON_DEFAULT, 1, option->symbol,
            option->default_value, &option->default_source) != CONFIT_OK) {
      return CONFIT_ERR_INTERNAL;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v4_apply_assignments(
    const ConfitV4Catalog *catalog, const ConfitV4Assignment *assignments,
    const char *const *override_paths, size_t assignment_count,
    ConfitV4Evaluation *evaluation,
    ConfitDiagnostic *diagnostic) {
  if (assignment_count > CONFIT_V4_MAX_OPTIONS ||
      (assignment_count != 0U && assignments == 0))
    return CONFIT_ERR_INVALID_ARGUMENT;
  for (size_t index = 0U; index < assignment_count; ++index) {
    const ConfitV4Assignment *assignment = &assignments[index];
    const ConfitV4Option *option =
        confit_v4_find_option(catalog, assignment->symbol);
    ConfitV4EffectiveValue *value;
    ConfitV4OwnedSpan source;
    int enabled;
    memset(&source, 0, sizeof(source));
    source.path = (char *)(assignment->source.path != 0
                              ? assignment->source.path
                              : "confit://request");
    source.line = assignment->source.line != 0U ? assignment->source.line : 1U;
    source.column =
        assignment->source.column != 0U ? assignment->source.column : 1U;
    if (option == 0 || assignment->value == 0 ||
        !confit_v4_option_value_valid(option, assignment->value, &enabled)) {
      confit_v4_set_diagnostic(
          diagnostic, CONFIT_ERR_SCHEMA, &source,
          option == 0 ? "assignment references an unknown Config v4 option"
                      : "assignment value is outside the option domain");
      return CONFIT_ERR_SCHEMA;
    }
    value = confit_v4_find_effective_mutable(evaluation, assignment->symbol);
    if (value == 0) return CONFIT_ERR_INTERNAL;
    for (size_t other = 0U; other < index; ++other) {
      if (strcmp(assignments[other].symbol, assignment->symbol) == 0) {
        if (override_paths == 0 || override_paths[index] == 0 ||
            value->source.path == 0 ||
            strcmp(override_paths[index], value->source.path) != 0) {
          confit_v4_set_diagnostic(
              diagnostic, CONFIT_ERR_CONFLICT, &source,
              "duplicate assignment needs an exact base-to-leaf override edge");
          return CONFIT_ERR_CONFLICT;
        }
        if (confit_v4_evaluation_add_reason(
                evaluation, CONFIT_V4_REASON_REQUEST, 1, option->symbol,
                override_paths[index], &source) != CONFIT_OK)
          return CONFIT_ERR_INTERNAL;
        break;
      }
    }
    free(value->value);
    value->value = confit_v4_copy(assignment->value);
    value->enabled = enabled;
    if (value->value == 0 ||
        !confit_v4_owned_span_set(&value->source, source.path, source.line,
                                  source.column)) return CONFIT_ERR_INTERNAL;
    if (confit_v4_evaluation_add_reason(
            evaluation, CONFIT_V4_REASON_REQUEST, 1, option->symbol,
            assignment->value, &source) != CONFIT_OK) return CONFIT_ERR_INTERNAL;
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v4_evaluate_prerequisites(
    const ConfitV4Catalog *catalog, ConfitV4Evaluation *evaluation,
    ConfitDiagnostic *diagnostic) {
  for (size_t index = 0U; index < catalog->option_count; ++index) {
    const ConfitV4Option *option = &catalog->options[index];
    const ConfitV4EffectiveValue *root =
        confit_v4_find_effective(evaluation, option->symbol);
    if (root == 0) return CONFIT_ERR_INTERNAL;
    for (size_t edge = 0U; edge < option->prerequisites.count; ++edge) {
      const ConfitV4EffectiveValue *required = confit_v4_find_effective(
          evaluation, option->prerequisites.items[edge]);
      const int satisfied = !root->enabled ||
                            (required != 0 && required->enabled);
      ConfitStatus status = confit_v4_evaluation_add_reason(
          evaluation, CONFIT_V4_REASON_PREREQUISITE, satisfied,
          option->symbol, option->prerequisites.items[edge],
          &option->prerequisites.spans[edge]);
      if (status != CONFIT_OK) return status;
      if (!satisfied) {
        confit_v4_set_diagnostic(
            diagnostic, CONFIT_ERR_DEPENDENCY,
            &option->prerequisites.spans[edge],
            "enabled option has a false constraints.all prerequisite");
        return CONFIT_ERR_DEPENDENCY;
      }
    }
    for (size_t edge = 0U; edge < option->visible_all.count; ++edge) {
      const ConfitV4EffectiveValue *required =
          confit_v4_find_effective(evaluation, option->visible_all.items[edge]);
      const int visible = required != 0 && required->enabled;
      ConfitStatus status = confit_v4_evaluation_add_reason(
          evaluation, CONFIT_V4_REASON_VISIBILITY, visible, option->symbol,
          option->visible_all.items[edge], &option->visible_all.spans[edge]);
      if (status != CONFIT_OK) return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v4_evaluate_choices(
    const ConfitV4Catalog *catalog, ConfitV4Evaluation *evaluation,
    ConfitDiagnostic *diagnostic) {
  for (size_t index = 0U; index < catalog->choice_count; ++index) {
    const ConfitV4Choice *choice = &catalog->choices[index];
    size_t enabled_count = 0U;
    char cause[64];
    for (size_t member = 0U; member < choice->members.count; ++member) {
      const ConfitV4EffectiveValue *value = confit_v4_find_effective(
          evaluation, choice->members.items[member]);
      if (value != 0 && value->enabled) ++enabled_count;
    }
    const int satisfied =
        choice->cardinality == CONFIT_V4_CHOICE_AT_MOST_ONE
            ? enabled_count <= 1U
            : enabled_count == 1U;
    (void)snprintf(cause, sizeof(cause), "enabled=%zu", enabled_count);
    if (confit_v4_evaluation_add_reason(
            evaluation, CONFIT_V4_REASON_CHOICE, satisfied, choice->symbol,
            cause, &choice->source) != CONFIT_OK) return CONFIT_ERR_INTERNAL;
    if (!satisfied) {
      confit_v4_set_diagnostic(diagnostic, CONFIT_ERR_CONFLICT,
                               &choice->source,
                               "choice cardinality is not satisfied");
      return CONFIT_ERR_CONFLICT;
    }
  }
  return CONFIT_OK;
}

static int confit_v4_all_enabled(const ConfitV4Evaluation *evaluation,
                                 const ConfitV4StringList *symbols) {
  for (size_t index = 0U; index < symbols->count; ++index) {
    const ConfitV4EffectiveValue *value =
        confit_v4_find_effective(evaluation, symbols->items[index]);
    if (value == 0 || !value->enabled) return 0;
  }
  return 1;
}

static ConfitStatus confit_v4_evaluate_rules(
    const ConfitV4Catalog *catalog, ConfitV4Evaluation *evaluation,
    ConfitDiagnostic *diagnostic) {
  for (size_t index = 0U; index < catalog->rule_count; ++index) {
    const ConfitV4Rule *rule = &catalog->rules[index];
    const int applies = confit_v4_all_enabled(evaluation, &rule->if_all);
    const int satisfied = !applies ||
                          confit_v4_all_enabled(evaluation,
                                                &rule->require_all);
    if (confit_v4_evaluation_add_reason(
            evaluation, CONFIT_V4_REASON_RULE, satisfied, rule->message,
            applies ? "if_all=true" : "if_all=false", &rule->source) !=
        CONFIT_OK) return CONFIT_ERR_INTERNAL;
    if (!satisfied) {
      confit_v4_set_diagnostic(diagnostic, CONFIT_ERR_CONFLICT, &rule->source,
                               rule->message);
      return CONFIT_ERR_CONFLICT;
    }
  }
  return CONFIT_OK;
}

static int confit_v4_provider_same(const ConfitV4Provider *provider,
                                   const char *namespace_name,
                                   uint32_t major) {
  return provider->major == major &&
         strcmp(provider->namespace_name, namespace_name) == 0;
}

static const ConfitV4ProviderChoice *confit_v4_find_provider_choice(
    const ConfitV4ProviderChoice *choices, size_t choice_count,
    const char *namespace_name, uint32_t major, int *out_duplicate) {
  const ConfitV4ProviderChoice *found = 0;
  *out_duplicate = 0;
  for (size_t index = 0U; index < choice_count; ++index) {
    if (choices[index].namespace_name != 0 &&
        strcmp(choices[index].namespace_name, namespace_name) == 0 &&
        choices[index].major == major) {
      if (found != 0) {
        *out_duplicate = 1;
        return found;
      }
      found = &choices[index];
    }
  }
  return found;
}

static ConfitStatus confit_v4_append_resolved_provider(
    ConfitV4Evaluation *evaluation, const char *namespace_name,
    uint32_t major, const char *option_symbol) {
  ConfitV4ResolvedProvider *grown = (ConfitV4ResolvedProvider *)realloc(
      evaluation->providers,
      (evaluation->provider_count + 1U) * sizeof(evaluation->providers[0]));
  ConfitV4ResolvedProvider provider;
  if (grown == 0) return CONFIT_ERR_INTERNAL;
  memset(&provider, 0, sizeof(provider));
  provider.namespace_name = confit_v4_copy(namespace_name);
  provider.option_symbol = confit_v4_copy(option_symbol);
  provider.major = major;
  if (provider.namespace_name == 0 || provider.option_symbol == 0) {
    free(provider.namespace_name);
    free(provider.option_symbol);
    return CONFIT_ERR_INTERNAL;
  }
  evaluation->providers = grown;
  evaluation->providers[evaluation->provider_count++] = provider;
  return CONFIT_OK;
}

static ConfitStatus confit_v4_evaluate_provider_group(
    const ConfitV4Catalog *catalog, const ConfitV4Provider *group,
    const ConfitV4ProviderChoice *choices, size_t choice_count,
    ConfitV4Evaluation *evaluation, ConfitDiagnostic *diagnostic) {
  const ConfitV4Option *selected[CONFIT_V4_MAX_OPTIONS];
  size_t selected_count = 0U;
  const ConfitV4ProviderChoice *choice;
  int duplicate_choice;
  for (size_t option_index = 0U; option_index < catalog->option_count;
       ++option_index) {
    const ConfitV4Option *option = &catalog->options[option_index];
    const ConfitV4EffectiveValue *value =
        confit_v4_find_effective(evaluation, option->symbol);
    if (value == 0 || !value->enabled) continue;
    for (size_t provider_index = 0U;
         provider_index < option->provider_count; ++provider_index) {
      if (confit_v4_provider_same(&option->providers[provider_index],
                                  group->namespace_name, group->major)) {
        selected[selected_count++] = option;
        break;
      }
    }
  }
  choice = confit_v4_find_provider_choice(
      choices, choice_count, group->namespace_name, group->major,
      &duplicate_choice);
  if (duplicate_choice) {
    ConfitV4OwnedSpan source = {(char *)choice->source.path,
                                choice->source.line, choice->source.column};
    confit_v4_set_diagnostic(
        diagnostic, CONFIT_ERR_CONFLICT, &source,
        "provider namespace has more than one explicit selection owner");
    return CONFIT_ERR_CONFLICT;
  }
  if (group->cardinality == CONFIT_V4_PROVIDER_CARDINALITY_MULTIPLE) {
    if (choice != 0 ||
        (selected_count == 0U &&
         group->absence == CONFIT_V4_PROVIDER_ABSENCE_FORBIDDEN)) {
      confit_v4_set_diagnostic(
          diagnostic, CONFIT_ERR_CONFLICT, &group->source,
          choice != 0
              ? "multiple provider group does not accept a first-choice owner"
              : "required multiple provider group is absent");
      return CONFIT_ERR_CONFLICT;
    }
    for (size_t index = 0U; index < selected_count; ++index) {
      if (confit_v4_evaluation_add_reason(
              evaluation, CONFIT_V4_REASON_PROVIDER, 1,
              group->namespace_name, selected[index]->symbol,
              &group->source) != CONFIT_OK) return CONFIT_ERR_INTERNAL;
    }
    return CONFIT_OK;
  }
  if (choice != 0) {
    const ConfitV4Option *chosen = 0;
    ConfitV4OwnedSpan source = {(char *)choice->source.path,
                                choice->source.line, choice->source.column};
    for (size_t index = 0U; index < selected_count; ++index) {
      if (choice->option_symbol != 0 &&
          strcmp(selected[index]->symbol, choice->option_symbol) == 0) {
        chosen = selected[index];
        break;
      }
    }
    if (chosen == 0) {
      confit_v4_evaluation_add_reason(
          evaluation, CONFIT_V4_REASON_AMBIGUITY, 0,
          group->namespace_name,
          choice->option_symbol != 0 ? choice->option_symbol : "<missing>",
          &source);
      confit_v4_set_diagnostic(
          diagnostic, CONFIT_ERR_CONFLICT, &source,
          "explicit provider choice is not an enabled candidate");
      return CONFIT_ERR_CONFLICT;
    }
    if (confit_v4_append_resolved_provider(
            evaluation, group->namespace_name, group->major,
            chosen->symbol) != CONFIT_OK ||
        confit_v4_evaluation_add_reason(
            evaluation, CONFIT_V4_REASON_PROVIDER, 1,
            group->namespace_name, chosen->symbol, &source) != CONFIT_OK) {
      return CONFIT_ERR_INTERNAL;
    }
    return CONFIT_OK;
  }
  if (selected_count == 1U) {
    if (confit_v4_append_resolved_provider(
            evaluation, group->namespace_name, group->major,
            selected[0]->symbol) != CONFIT_OK ||
        confit_v4_evaluation_add_reason(
            evaluation, CONFIT_V4_REASON_PROVIDER, 1,
            group->namespace_name, selected[0]->symbol,
            &group->source) != CONFIT_OK) return CONFIT_ERR_INTERNAL;
    return CONFIT_OK;
  }
  if (selected_count == 0U &&
      group->absence == CONFIT_V4_PROVIDER_ABSENCE_ALLOWED) {
    return confit_v4_evaluation_add_reason(
        evaluation, CONFIT_V4_REASON_PROVIDER, 1, group->namespace_name,
        "absent", &group->source);
  }
  if (confit_v4_evaluation_add_reason(
          evaluation, CONFIT_V4_REASON_AMBIGUITY, 0,
          group->namespace_name,
          selected_count == 0U ? "absent" : "multiple-selected",
          &group->source) != CONFIT_OK) return CONFIT_ERR_INTERNAL;
  confit_v4_set_diagnostic(
      diagnostic, CONFIT_ERR_CONFLICT, &group->source,
      selected_count == 0U ? "required single provider is absent"
                           : "single provider selection is ambiguous; first-match is forbidden");
  return CONFIT_ERR_CONFLICT;
}

static ConfitStatus confit_v4_evaluate_providers(
    const ConfitV4Catalog *catalog, const ConfitV4ProviderChoice *choices,
    size_t choice_count, ConfitV4Evaluation *evaluation,
    ConfitDiagnostic *diagnostic) {
  if (choice_count > CONFIT_V4_MAX_PROVIDERS ||
      (choice_count != 0U && choices == 0)) return CONFIT_ERR_INVALID_ARGUMENT;
  for (size_t option_index = 0U; option_index < catalog->option_count;
       ++option_index) {
    const ConfitV4Option *option = &catalog->options[option_index];
    for (size_t provider_index = 0U;
         provider_index < option->provider_count; ++provider_index) {
      const ConfitV4Provider *group = &option->providers[provider_index];
      int seen = 0;
      for (size_t earlier_option = 0U; earlier_option <= option_index;
           ++earlier_option) {
        const ConfitV4Option *candidate = &catalog->options[earlier_option];
        const size_t limit = earlier_option == option_index
                                 ? provider_index
                                 : candidate->provider_count;
        for (size_t earlier_provider = 0U;
             earlier_provider < limit; ++earlier_provider) {
          if (confit_v4_provider_same(&candidate->providers[earlier_provider],
                                      group->namespace_name, group->major)) {
            seen = 1;
            break;
          }
        }
        if (seen) break;
      }
      if (!seen) {
        ConfitStatus status = confit_v4_evaluate_provider_group(
            catalog, group, choices, choice_count, evaluation, diagnostic);
        if (status != CONFIT_OK) return status;
      }
    }
  }
  for (size_t choice_index = 0U; choice_index < choice_count; ++choice_index) {
    int found = 0;
    for (size_t option_index = 0U; option_index < catalog->option_count;
         ++option_index) {
      const ConfitV4Option *option = &catalog->options[option_index];
      for (size_t provider_index = 0U;
           provider_index < option->provider_count; ++provider_index) {
        if (choices[choice_index].namespace_name != 0 &&
            confit_v4_provider_same(&option->providers[provider_index],
                                    choices[choice_index].namespace_name,
                                    choices[choice_index].major)) {
          found = 1;
          break;
        }
      }
      if (found) break;
    }
    if (!found) {
      ConfitV4OwnedSpan source = {(char *)choices[choice_index].source.path,
                                  choices[choice_index].source.line,
                                  choices[choice_index].source.column};
      confit_v4_set_diagnostic(
          diagnostic, CONFIT_ERR_CONFLICT, &source,
          "explicit provider choice references an unknown namespace/major");
      return CONFIT_ERR_CONFLICT;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v4_evaluate_internal(
    const ConfitV4Catalog *catalog, const ConfitV4Assignment *assignments,
    const char *const *override_paths, size_t assignment_count,
    const ConfitV4ProviderChoice *provider_choices,
    size_t provider_choice_count, ConfitV4Evaluation **out_evaluation,
    ConfitDiagnostic *diagnostic) {
  ConfitV4Evaluation *evaluation;
  ConfitStatus status;
  if (catalog == 0 || out_evaluation == 0) return CONFIT_ERR_INVALID_ARGUMENT;
  *out_evaluation = 0;
  evaluation = (ConfitV4Evaluation *)calloc(1U, sizeof(*evaluation));
  if (evaluation == 0) return CONFIT_ERR_INTERNAL;
  status = confit_v4_initialize_values(catalog, evaluation);
  if (status == CONFIT_OK)
    status = confit_v4_apply_assignments(catalog, assignments, override_paths,
                                         assignment_count, evaluation,
                                         diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_evaluate_prerequisites(catalog, evaluation, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_evaluate_choices(catalog, evaluation, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_evaluate_rules(catalog, evaluation, diagnostic);
  if (status == CONFIT_OK)
    status = confit_v4_evaluate_providers(
        catalog, provider_choices, provider_choice_count, evaluation,
        diagnostic);
  *out_evaluation = evaluation;
  return status;
}

ConfitStatus confit_v4_evaluate(
    const ConfitV4Catalog *catalog, const ConfitV4Assignment *assignments,
    size_t assignment_count, const ConfitV4ProviderChoice *provider_choices,
    size_t provider_choice_count, ConfitV4Evaluation **out_evaluation,
    ConfitDiagnostic *diagnostic) {
  return confit_v4_evaluate_internal(
      catalog, assignments, 0, assignment_count, provider_choices,
      provider_choice_count, out_evaluation, diagnostic);
}

ConfitStatus confit_v4_evaluate_layered(
    const ConfitV4Catalog *catalog,
    const ConfitV4LayeredAssignment *assignments, size_t assignment_count,
    const ConfitV4ProviderChoice *provider_choices,
    size_t provider_choice_count, ConfitV4Evaluation **out_evaluation,
    ConfitDiagnostic *diagnostic) {
  ConfitV4Assignment *plain = 0;
  const char **overrides = 0;
  ConfitStatus status;
  if (assignment_count != 0U && assignments == 0)
    return CONFIT_ERR_INVALID_ARGUMENT;
  if (assignment_count != 0U) {
    plain = (ConfitV4Assignment *)calloc(assignment_count, sizeof(plain[0]));
    overrides = (const char **)calloc(assignment_count, sizeof(overrides[0]));
    if (plain == 0 || overrides == 0) {
      free(plain);
      free(overrides);
      return CONFIT_ERR_INTERNAL;
    }
    for (size_t index = 0U; index < assignment_count; ++index) {
      plain[index] = assignments[index].assignment;
      overrides[index] = assignments[index].overrides_source_path;
    }
  }
  status = confit_v4_evaluate_internal(
      catalog, plain, overrides, assignment_count, provider_choices,
      provider_choice_count, out_evaluation, diagnostic);
  free(plain);
  free(overrides);
  return status;
}

void confit_v4_evaluation_free(ConfitV4Evaluation *evaluation) {
  if (evaluation == 0) return;
  for (size_t index = 0U; index < evaluation->value_count; ++index) {
    free(evaluation->values[index].symbol);
    free(evaluation->values[index].value);
    confit_v4_owned_span_clear(&evaluation->values[index].source);
  }
  free(evaluation->values);
  for (size_t index = 0U; index < evaluation->reason_count; ++index) {
    free(evaluation->reasons[index].subject);
    free(evaluation->reasons[index].cause);
    confit_v4_owned_span_clear(&evaluation->reasons[index].source);
  }
  free(evaluation->reasons);
  for (size_t index = 0U; index < evaluation->provider_count; ++index) {
    free(evaluation->providers[index].namespace_name);
    free(evaluation->providers[index].option_symbol);
  }
  free(evaluation->providers);
  free(evaluation);
}

const char *confit_v4_evaluation_value(const ConfitV4Evaluation *evaluation,
                                       const char *symbol) {
  const ConfitV4EffectiveValue *value =
      confit_v4_find_effective(evaluation, symbol);
  return value != 0 ? value->value : 0;
}

size_t confit_v4_evaluation_value_count(
    const ConfitV4Evaluation *evaluation) {
  return evaluation != 0 ? evaluation->value_count : 0U;
}

int confit_v4_evaluation_value_at(
    const ConfitV4Evaluation *evaluation, size_t index, const char **out_symbol,
    const char **out_value, int *out_enabled, ConfitV4SourceSpan *out_source) {
  const ConfitV4EffectiveValue *value;
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

size_t confit_v4_evaluation_reason_count(
    const ConfitV4Evaluation *evaluation) {
  return evaluation != 0 ? evaluation->reason_count : 0U;
}

int confit_v4_evaluation_reason(const ConfitV4Evaluation *evaluation,
                                size_t index,
                                ConfitV4ReasonView *out_reason) {
  const ConfitV4Reason *reason;
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

const char *confit_v4_evaluation_single_provider(
    const ConfitV4Evaluation *evaluation, const char *namespace_name,
    uint32_t major) {
  if (evaluation == 0 || namespace_name == 0) return 0;
  for (size_t index = 0U; index < evaluation->provider_count; ++index) {
    if (evaluation->providers[index].major == major &&
        strcmp(evaluation->providers[index].namespace_name,
               namespace_name) == 0) {
      return evaluation->providers[index].option_symbol;
    }
  }
  return 0;
}
