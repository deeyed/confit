#include "ledger_internal.h"

#include <stdlib.h>
#include <string.h>

static const char kAllocationFailed[] = "failed to allocate v2 assignment ledger";
static const char kInvalidLedgerArgument[] =
    "invalid schema v2 assignment ledger argument";
static const char kDuplicateUserAssignment[] =
    "duplicate schema v2 user override";
static const char kDuplicateProfileTransactionAssignment[] =
    "duplicate schema v2 profile transaction assignment";
static const char kBaseProfileTarget[] =
    "schema v2 base profile cannot select target";
static const char kAmbiguousAssignment[] =
    "ambiguous schema v2 assignment ownership";

enum {
  CONFIT_V2_LEDGER_PRECEDENCE_SCHEMA_DEFAULT = 0,
  CONFIT_V2_LEDGER_PRECEDENCE_DOMAIN = 1,
  CONFIT_V2_LEDGER_PRECEDENCE_PROFILE_TRANSACTION = 2,
  CONFIT_V2_LEDGER_PRECEDENCE_USER = 3,
};

static int confit_v2_ledger_entry_compare(const void *left, const void *right) {
  const ConfitV2LedgerEntry *left_entry = (const ConfitV2LedgerEntry *)left;
  const ConfitV2LedgerEntry *right_entry = (const ConfitV2LedgerEntry *)right;
  int compared = strcmp(left_entry->symbol->id, right_entry->symbol->id);

  if (compared != 0) {
    return compared;
  }
  if (left_entry->precedence != right_entry->precedence) {
    return left_entry->precedence < right_entry->precedence ? -1 : 1;
  }
  if (left_entry->domain != right_entry->domain) {
    return left_entry->domain < right_entry->domain ? -1 : 1;
  }
  if (left_entry->declaration_order != right_entry->declaration_order) {
    return left_entry->declaration_order < right_entry->declaration_order ? -1
                                                                          : 1;
  }
  if (left_entry->origin != right_entry->origin) {
    return left_entry->origin < right_entry->origin ? -1 : 1;
  }
  if (left_entry->source_path == 0) {
    return right_entry->source_path == 0 ? 0 : -1;
  }
  if (right_entry->source_path == 0) {
    return 1;
  }
  compared = strcmp(left_entry->source_path, right_entry->source_path);
  if (compared != 0) {
    return compared;
  }
  if (left_entry->source_line != right_entry->source_line) {
    return left_entry->source_line < right_entry->source_line ? -1 : 1;
  }
  if (left_entry->source_column != right_entry->source_column) {
    return left_entry->source_column < right_entry->source_column ? -1 : 1;
  }
  return 0;
}

static void confit_v2_ledger_entry_clear(ConfitV2LedgerEntry *entry) {
  if (entry == 0) {
    return;
  }
  confit_v2_ledger_value_clear(&entry->value);
  free((char *)entry->source_path);
  memset(entry, 0, sizeof(*entry));
}

static ConfitStatus confit_v2_ledger_append(
    ConfitV2AssignmentLedger *ledger, const ConfitV2Symbol *symbol,
    const ConfitV2Value *value, ConfitV2AssignmentOrigin origin, int is_unset,
    size_t precedence, size_t declaration_order, const char *source_path,
    size_t source_line, size_t source_column, ConfitDiagnostic *diagnostic) {
  ConfitV2LedgerEntry *grown;
  ConfitV2LedgerEntry *entry;

  if (ledger->entry_count == SIZE_MAX / sizeof(*ledger->entries)) {
    confit_v2_ledger_diagnostic(source_path, source_line, source_column,
                                CONFIT_ERR_INTERNAL, kAllocationFailed,
                                diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  grown = (ConfitV2LedgerEntry *)realloc(
      ledger->entries, (ledger->entry_count + 1U) * sizeof(*ledger->entries));
  if (grown == 0) {
    confit_v2_ledger_diagnostic(source_path, source_line, source_column,
                                CONFIT_ERR_INTERNAL, kAllocationFailed,
                                diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  ledger->entries = grown;
  entry = &ledger->entries[ledger->entry_count];
  memset(entry, 0, sizeof(*entry));
  entry->symbol = symbol;
  entry->origin = origin;
  entry->domain = symbol->write_domain;
  entry->is_unset = is_unset;
  entry->precedence = precedence;
  entry->declaration_order = declaration_order;
  entry->source_line = source_line;
  entry->source_column = source_column;
  entry->source_path = confit_v2_ledger_strdup(source_path);
  if (source_path != 0 && entry->source_path == 0) {
    confit_v2_ledger_entry_clear(entry);
    confit_v2_ledger_diagnostic(source_path, source_line, source_column,
                                CONFIT_ERR_INTERNAL, kAllocationFailed,
                                diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  if (!is_unset && value != 0 &&
      confit_v2_ledger_value_copy(&entry->value, value) != CONFIT_OK) {
    confit_v2_ledger_entry_clear(entry);
    confit_v2_ledger_diagnostic(source_path, source_line, source_column,
                                CONFIT_ERR_INTERNAL, kAllocationFailed,
                                diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  ledger->entry_count += 1U;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_ledger_add_schema_defaults(
    const ConfitV2LinkedProject *linked, ConfitV2AssignmentLedger *ledger,
    ConfitDiagnostic *diagnostic) {
  const ConfitV2Project *project = confit_v2_linked_project_source(linked);
  size_t index;

  for (index = 0U; index < project->symbol_count; ++index) {
    const ConfitV2Symbol *symbol = &project->symbols[index];
    const ConfitV2Assignment *assignment = &symbol->default_value;
    ConfitStatus status;

    if (symbol->write_domain == CONFIT_V2_WRITE_DOMAIN_COMPUTED ||
        !assignment->is_set) {
      continue;
    }
    status = confit_v2_ledger_append(
        ledger, symbol, &assignment->value,
        CONFIT_V2_ASSIGNMENT_ORIGIN_SCHEMA_DEFAULT, 0,
        CONFIT_V2_LEDGER_PRECEDENCE_SCHEMA_DEFAULT, index, assignment->span.path,
        assignment->span.line, assignment->span.column, diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_ledger_add_input_chain(
    const ConfitV2InputDocument *const *chain, size_t chain_count,
    ConfitV2AssignmentOrigin origin, ConfitV2AssignmentLedger *ledger,
    ConfitDiagnostic *diagnostic) {
  size_t chain_index;

  for (chain_index = 0U; chain_index < chain_count; ++chain_index) {
    const ConfitV2InputDocument *document = chain[chain_index];
    size_t assignment_index;

    for (assignment_index = 0U; assignment_index < document->assignment_count;
         ++assignment_index) {
      const ConfitV2InputAssignment *assignment =
          &document->assignments[assignment_index];
      size_t declaration_order;
      ConfitStatus status;

      if (chain_index > SIZE_MAX / 1000000U ||
          assignment->declaration_order >= 1000000U) {
        confit_v2_ledger_diagnostic(document->path, assignment->line,
                                    assignment->column, CONFIT_ERR_INTERNAL,
                                    kAllocationFailed, diagnostic);
        return CONFIT_ERR_INTERNAL;
      }
      declaration_order = chain_index * 1000000U + assignment->declaration_order;
      status = confit_v2_ledger_append(
          ledger, assignment->symbol,
          assignment->is_unset ? 0 : &assignment->value,
          assignment->is_unset ? CONFIT_V2_ASSIGNMENT_ORIGIN_UNSET : origin,
          assignment->is_unset, CONFIT_V2_LEDGER_PRECEDENCE_DOMAIN,
          declaration_order, document->path, assignment->line, assignment->column,
          diagnostic);
      if (status != CONFIT_OK) {
        return status;
      }
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_ledger_set_target_selection(
    ConfitV2AssignmentLedger *ledger, const char *name,
    ConfitV2TargetSelectionOrigin origin, const char *source_path,
    size_t source_line, size_t source_column, ConfitDiagnostic *diagnostic) {
  ledger->target_name = confit_v2_ledger_strdup(name);
  ledger->target_selection.source_path = confit_v2_ledger_strdup(source_path);
  if (ledger->target_name == 0 ||
      (source_path != 0 && ledger->target_selection.source_path == 0)) {
    free(ledger->target_name);
    free((char *)ledger->target_selection.source_path);
    ledger->target_name = 0;
    ledger->target_selection.source_path = 0;
    confit_v2_ledger_diagnostic(source_path, source_line, source_column,
                                CONFIT_ERR_INTERNAL, kAllocationFailed,
                                diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  ledger->target_selection.name = ledger->target_name;
  ledger->target_selection.origin = origin;
  ledger->target_selection.source_line = source_line;
  ledger->target_selection.source_column = source_column;
  return CONFIT_OK;
}

static ConfitStatus confit_v2_ledger_add_user_overrides(
    const ConfitV2LinkedProject *linked, const ConfitV2LedgerOptions *options,
    ConfitV2AssignmentLedger *ledger, ConfitDiagnostic *diagnostic) {
  size_t index;

  if (options == 0) {
    return CONFIT_OK;
  }
  for (index = 0U; index < options->user_override_count; ++index) {
    const ConfitV2UserOverride *override = &options->user_overrides[index];
    const ConfitV2Symbol *symbol;
    ConfitV2WriteRequest request;
    ConfitV2Value value;
    const char *path;
    size_t earlier;
    ConfitStatus status;

    if (override->option_id == 0 || override->option_id[0] == '\0' ||
        override->value_text == 0) {
      confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INVALID_ARGUMENT,
                                  kInvalidLedgerArgument, diagnostic);
      return CONFIT_ERR_INVALID_ARGUMENT;
    }
    for (earlier = 0U; earlier < index; ++earlier) {
      if (options->user_overrides[earlier].option_id != 0 &&
          strcmp(options->user_overrides[earlier].option_id,
                 override->option_id) == 0) {
        confit_v2_ledger_diagnostic(override->span.path, override->span.line,
                                    override->span.column, CONFIT_ERR_SCHEMA,
                                    kDuplicateUserAssignment, diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
    }
    symbol = confit_v2_linked_project_find_symbol(linked, override->option_id);
    if (symbol == 0) {
      memset(&request, 0, sizeof(request));
      request.option_id = override->option_id;
      request.writer = CONFIT_V2_ASSIGNMENT_WRITER_USER;
      request.span = override->span;
      return confit_v2_linked_project_validate_write(linked, &request,
                                                      diagnostic);
    }
    memset(&request, 0, sizeof(request));
    request.option_id = override->option_id;
    request.writer = CONFIT_V2_ASSIGNMENT_WRITER_USER;
    request.span = override->span;
    status = confit_v2_linked_project_validate_write(linked, &request,
                                                      diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    memset(&value, 0, sizeof(value));
    status = confit_v2_ledger_parse_user_value(symbol, override->value_text,
                                                &value, diagnostic);
    if (status == CONFIT_OK) {
      path = override->span.path != 0 ? override->span.path : "cli --set";
      status = confit_v2_ledger_append(
          ledger, symbol, &value, CONFIT_V2_ASSIGNMENT_ORIGIN_USER, 0,
          CONFIT_V2_LEDGER_PRECEDENCE_USER, index, path, override->span.line,
          override->span.column, diagnostic);
    }
    confit_v2_ledger_value_clear(&value);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_ledger_add_profile_overrides(
    const ConfitV2LinkedProject *linked, const ConfitV2LedgerOptions *options,
    ConfitV2AssignmentLedger *ledger, ConfitDiagnostic *diagnostic) {
  size_t index;

  if (options == 0) {
    return CONFIT_OK;
  }
  for (index = 0U; index < options->profile_override_count; ++index) {
    const ConfitV2ProfileOverride *override = &options->profile_overrides[index];
    const ConfitV2Symbol *symbol;
    ConfitV2WriteRequest request;
    ConfitV2Value value;
    const char *path;
    size_t earlier;
    ConfitStatus status;

    if (override->option_id == 0 || override->option_id[0] == '\0' ||
        override->value_text == 0) {
      confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INVALID_ARGUMENT,
                                  kInvalidLedgerArgument, diagnostic);
      return CONFIT_ERR_INVALID_ARGUMENT;
    }
    for (earlier = 0U; earlier < index; ++earlier) {
      if (options->profile_overrides[earlier].option_id != 0 &&
          strcmp(options->profile_overrides[earlier].option_id,
                 override->option_id) == 0) {
        confit_v2_ledger_diagnostic(override->span.path, override->span.line,
                                    override->span.column, CONFIT_ERR_SCHEMA,
                                    kDuplicateProfileTransactionAssignment,
                                    diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
    }
    symbol = confit_v2_linked_project_find_symbol(linked, override->option_id);
    memset(&request, 0, sizeof(request));
    request.option_id = override->option_id;
    request.writer = CONFIT_V2_ASSIGNMENT_WRITER_PROFILE;
    request.span = override->span;
    status = confit_v2_linked_project_validate_write(linked, &request,
                                                      diagnostic);
    if (status != CONFIT_OK) {
      return status;
    }
    if (symbol == 0) {
      return CONFIT_ERR_SCHEMA;
    }
    memset(&value, 0, sizeof(value));
    status = confit_v2_ledger_parse_user_value(symbol, override->value_text,
                                                &value, diagnostic);
    if (status == CONFIT_OK) {
      path = override->span.path != 0 ? override->span.path
                                      : "tui profile transaction";
      status = confit_v2_ledger_append(
          ledger, symbol, &value, CONFIT_V2_ASSIGNMENT_ORIGIN_PROFILE, 0,
          CONFIT_V2_LEDGER_PRECEDENCE_PROFILE_TRANSACTION, index, path,
          override->span.line, override->span.column, diagnostic);
    }
    confit_v2_ledger_value_clear(&value);
    if (status != CONFIT_OK) {
      return status;
    }
  }
  return CONFIT_OK;
}

static ConfitStatus confit_v2_ledger_mark_winners(
    ConfitV2AssignmentLedger *ledger, ConfitDiagnostic *diagnostic) {
  size_t begin = 0U;

  qsort(ledger->entries, ledger->entry_count, sizeof(*ledger->entries),
        confit_v2_ledger_entry_compare);
  while (begin < ledger->entry_count) {
    size_t end = begin + 1U;
    size_t index;

    while (end < ledger->entry_count &&
           strcmp(ledger->entries[begin].symbol->id,
                  ledger->entries[end].symbol->id) == 0) {
      ++end;
    }
    for (index = begin; index < end; ++index) {
      ledger->entries[index].wins = 0;
      if (index > begin &&
          ledger->entries[index - 1U].precedence ==
              ledger->entries[index].precedence &&
          ledger->entries[index - 1U].declaration_order ==
              ledger->entries[index].declaration_order &&
          ledger->entries[index - 1U].origin != ledger->entries[index].origin) {
        confit_v2_ledger_diagnostic(
            ledger->entries[index].source_path, ledger->entries[index].source_line,
            ledger->entries[index].source_column, CONFIT_ERR_SCHEMA,
            kAmbiguousAssignment, diagnostic);
        return CONFIT_ERR_SCHEMA;
      }
    }
    ledger->entries[end - 1U].wins = 1;
    begin = end;
  }
  return CONFIT_OK;
}

ConfitStatus confit_v2_assignment_ledger_build(
    const ConfitV2CompiledStructure *compiled,
    const ConfitV2LedgerOptions *options, ConfitV2AssignmentLedger **out_ledger,
    ConfitDiagnostic *diagnostic) {
  const ConfitV2LinkedProject *linked;
  const ConfitV2Project *project;
  ConfitV2InputCatalog profiles;
  ConfitV2InputCatalog targets;
  const ConfitV2InputDocument **profile_chain = 0;
  const ConfitV2InputDocument **target_chain = 0;
  size_t profile_count = 0U;
  size_t target_count = 0U;
  const char *selected_target = 0;
  ConfitV2TargetSelectionOrigin target_origin =
      CONFIT_V2_TARGET_SELECTION_PROJECT_DEFAULT;
  const char *target_source_path = 0;
  size_t target_source_line = 0U;
  size_t target_source_column = 0U;
  ConfitV2AssignmentLedger *ledger;
  ConfitStatus status;
  size_t index;

  if (out_ledger == 0) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INVALID_ARGUMENT,
                                kInvalidLedgerArgument, diagnostic);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  *out_ledger = 0;
  if (compiled == 0 ||
      (options != 0 && options->profile_override_count > 0U &&
       options->profile_overrides == 0) ||
      (options != 0 && options->user_override_count > 0U &&
       options->user_overrides == 0)) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INVALID_ARGUMENT,
                                kInvalidLedgerArgument, diagnostic);
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  linked = confit_v2_compiled_structure_source(compiled);
  project = confit_v2_linked_project_source(linked);
  ledger = (ConfitV2AssignmentLedger *)calloc(1U, sizeof(*ledger));
  if (ledger == 0) {
    confit_v2_ledger_diagnostic(0, 0U, 0U, CONFIT_ERR_INTERNAL,
                                kAllocationFailed, diagnostic);
    return CONFIT_ERR_INTERNAL;
  }
  ledger->compiled = compiled;
  memset(&profiles, 0, sizeof(profiles));
  memset(&targets, 0, sizeof(targets));
  status = confit_v2_input_catalog_load(compiled, CONFIT_V2_INPUT_KIND_PROFILE,
                                         &profiles, diagnostic);
  if (status != CONFIT_OK) {
    goto fail;
  }
  status = confit_v2_input_catalog_load(compiled, CONFIT_V2_INPUT_KIND_TARGET,
                                         &targets, diagnostic);
  if (status != CONFIT_OK) {
    goto fail;
  }
  if (options != 0 && options->profile_name != 0) {
    status = confit_v2_input_catalog_build_chain(&profiles, options->profile_name,
                                                 &profile_chain, &profile_count,
                                                 diagnostic);
    if (status != CONFIT_OK) {
      goto fail;
    }
    ledger->profile_name = confit_v2_ledger_strdup(options->profile_name);
    if (ledger->profile_name == 0) {
      status = CONFIT_ERR_INTERNAL;
      confit_v2_ledger_diagnostic(0, 0U, 0U, status, kAllocationFailed,
                                  diagnostic);
      goto fail;
    }
    for (index = 0U; index + 1U < profile_count; ++index) {
      if (profile_chain[index]->target != 0) {
        status = CONFIT_ERR_SCHEMA;
        confit_v2_ledger_diagnostic(profile_chain[index]->path, 0U, 0U, status,
                                    kBaseProfileTarget, diagnostic);
        goto fail;
      }
    }
  }
  if (options != 0 && options->target_name != 0) {
    selected_target = options->target_name;
    target_origin = CONFIT_V2_TARGET_SELECTION_EXPLICIT;
    target_source_path = options->target_span.path != 0
                             ? options->target_span.path
                             : "cli --target";
    target_source_line = options->target_span.line;
    target_source_column = options->target_span.column;
  } else if (profile_count > 0U && profile_chain[profile_count - 1U]->target != 0) {
    selected_target = profile_chain[profile_count - 1U]->target;
    target_origin = CONFIT_V2_TARGET_SELECTION_PROFILE;
    target_source_path = profile_chain[profile_count - 1U]->path;
    target_source_line = profile_chain[profile_count - 1U]->target_line;
    target_source_column = profile_chain[profile_count - 1U]->target_column;
  } else {
    selected_target = project->default_target;
    target_source_path = project->default_target_span.path;
    target_source_line = project->default_target_span.line;
    target_source_column = project->default_target_span.column;
  }
  if (selected_target != 0) {
    status = confit_v2_input_catalog_build_chain(&targets, selected_target,
                                                 &target_chain, &target_count,
                                                 diagnostic);
    if (status != CONFIT_OK) {
      goto fail;
    }
    status = confit_v2_ledger_set_target_selection(
        ledger, selected_target, target_origin, target_source_path,
        target_source_line, target_source_column, diagnostic);
    if (status != CONFIT_OK) {
      goto fail;
    }
  }
  status = confit_v2_ledger_add_schema_defaults(linked, ledger, diagnostic);
  if (status == CONFIT_OK) {
    status = confit_v2_ledger_add_input_chain(
        target_chain, target_count, CONFIT_V2_ASSIGNMENT_ORIGIN_TARGET, ledger,
        diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_ledger_add_input_chain(
        profile_chain, profile_count, CONFIT_V2_ASSIGNMENT_ORIGIN_PROFILE, ledger,
        diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_ledger_add_profile_overrides(linked, options, ledger,
                                                     diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_ledger_add_user_overrides(linked, options, ledger,
                                                  diagnostic);
  }
  if (status == CONFIT_OK) {
    status = confit_v2_ledger_mark_winners(ledger, diagnostic);
  }
  if (status != CONFIT_OK) {
    goto fail;
  }
  confit_v2_input_chain_free(profile_chain);
  confit_v2_input_chain_free(target_chain);
  confit_v2_input_catalog_clear(&profiles);
  confit_v2_input_catalog_clear(&targets);
  *out_ledger = ledger;
  return CONFIT_OK;

fail:
  confit_v2_input_chain_free(profile_chain);
  confit_v2_input_chain_free(target_chain);
  confit_v2_input_catalog_clear(&profiles);
  confit_v2_input_catalog_clear(&targets);
  confit_v2_assignment_ledger_free(ledger);
  return status;
}

void confit_v2_assignment_ledger_free(ConfitV2AssignmentLedger *ledger) {
  size_t index;

  if (ledger == 0) {
    return;
  }
  for (index = 0U; index < ledger->entry_count; ++index) {
    confit_v2_ledger_entry_clear(&ledger->entries[index]);
  }
  free(ledger->entries);
  free(ledger->profile_name);
  free((char *)ledger->target_selection.source_path);
  free(ledger->target_name);
  free(ledger);
}

const ConfitV2CompiledStructure *confit_v2_assignment_ledger_source(
    const ConfitV2AssignmentLedger *ledger) {
  return ledger != 0 ? ledger->compiled : 0;
}

size_t confit_v2_assignment_ledger_entry_count(
    const ConfitV2AssignmentLedger *ledger) {
  return ledger != 0 ? ledger->entry_count : 0U;
}

const ConfitV2LedgerEntry *confit_v2_assignment_ledger_entry_at(
    const ConfitV2AssignmentLedger *ledger, size_t index) {
  if (ledger == 0 || index >= ledger->entry_count) {
    return 0;
  }
  return &ledger->entries[index];
}

const ConfitV2LedgerEntry *confit_v2_assignment_ledger_requested(
    const ConfitV2AssignmentLedger *ledger, const char *option_id) {
  size_t low = 0U;
  size_t high;
  size_t index;

  if (ledger == 0 || option_id == 0) {
    return 0;
  }
  high = ledger->entry_count;
  while (low < high) {
    const size_t middle = low + (high - low) / 2U;
    if (strcmp(ledger->entries[middle].symbol->id, option_id) < 0) {
      low = middle + 1U;
    } else {
      high = middle;
    }
  }
  for (index = low;
       index < ledger->entry_count &&
       strcmp(ledger->entries[index].symbol->id, option_id) == 0;
       ++index) {
    if (ledger->entries[index].wins) {
      return &ledger->entries[index];
    }
  }
  return 0;
}

const char *confit_v2_assignment_ledger_profile_name(
    const ConfitV2AssignmentLedger *ledger) {
  return ledger != 0 ? ledger->profile_name : 0;
}

const char *confit_v2_assignment_ledger_target_name(
    const ConfitV2AssignmentLedger *ledger) {
  return ledger != 0 ? ledger->target_name : 0;
}

const ConfitV2TargetSelection *confit_v2_assignment_ledger_target_selection(
    const ConfitV2AssignmentLedger *ledger) {
  return ledger != 0 && ledger->target_selection.name != 0
             ? &ledger->target_selection
             : 0;
}

static uint64_t confit_v2_ledger_hash_bytes(uint64_t hash, const void *data,
                                             size_t size) {
  const unsigned char *bytes = (const unsigned char *)data;
  size_t index;

  for (index = 0U; index < size; ++index) {
    hash ^= (uint64_t)bytes[index];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

static uint64_t confit_v2_ledger_hash_u64(uint64_t hash, uint64_t value) {
  unsigned char bytes[8];
  size_t index;

  for (index = 0U; index < sizeof(bytes); ++index) {
    bytes[index] = (unsigned char)(value & UINT64_C(0xff));
    value >>= 8U;
  }
  return confit_v2_ledger_hash_bytes(hash, bytes, sizeof(bytes));
}

static uint64_t confit_v2_ledger_hash_string(uint64_t hash, const char *text) {
  const size_t size = text != 0 ? strlen(text) : 0U;

  hash = confit_v2_ledger_hash_u64(hash, (uint64_t)size);
  return size > 0U ? confit_v2_ledger_hash_bytes(hash, text, size) : hash;
}

static uint64_t confit_v2_ledger_hash_value(uint64_t hash,
                                             const ConfitV2Value *value) {
  size_t index;

  hash = confit_v2_ledger_hash_u64(hash, (uint64_t)value->kind);
  switch (value->kind) {
  case CONFIT_V2_VALUE_BOOL:
    return confit_v2_ledger_hash_u64(hash, (uint64_t)value->as.bool_value);
  case CONFIT_V2_VALUE_TRISTATE:
    return confit_v2_ledger_hash_u64(hash,
                                     (uint64_t)(unsigned char)value->as.tristate_value);
  case CONFIT_V2_VALUE_INT:
    return confit_v2_ledger_hash_u64(hash, (uint64_t)value->as.int_value);
  case CONFIT_V2_VALUE_UINT:
    return confit_v2_ledger_hash_u64(hash, value->as.uint_value);
  case CONFIT_V2_VALUE_FLOAT: {
    uint64_t bits = 0U;
    memcpy(&bits, &value->as.float_value, sizeof(bits));
    return confit_v2_ledger_hash_u64(hash, bits);
  }
  case CONFIT_V2_VALUE_STRING:
    return confit_v2_ledger_hash_string(hash, value->as.string_value);
  case CONFIT_V2_VALUE_STRING_LIST:
    hash = confit_v2_ledger_hash_u64(
        hash, (uint64_t)value->as.string_list.count);
    for (index = 0U; index < value->as.string_list.count; ++index) {
      hash = confit_v2_ledger_hash_string(hash, value->as.string_list.items[index]);
    }
    return hash;
  case CONFIT_V2_VALUE_UNSET:
  default:
    return hash;
  }
}

ConfitStatus confit_v2_assignment_ledger_hash(
    const ConfitV2AssignmentLedger *ledger, uint64_t *out_hash) {
  uint64_t hash = UINT64_C(1469598103934665603);
  size_t index;

  if (ledger == 0 || out_hash == 0) {
    return CONFIT_ERR_INVALID_ARGUMENT;
  }
  for (index = 0U; index < ledger->entry_count; ++index) {
    const ConfitV2LedgerEntry *entry = &ledger->entries[index];

    hash = confit_v2_ledger_hash_string(hash, entry->symbol->id);
    hash = confit_v2_ledger_hash_u64(hash, (uint64_t)entry->origin);
    hash = confit_v2_ledger_hash_u64(hash, (uint64_t)entry->domain);
    hash = confit_v2_ledger_hash_u64(hash, (uint64_t)entry->is_unset);
    hash = confit_v2_ledger_hash_u64(hash, (uint64_t)entry->wins);
    hash = confit_v2_ledger_hash_u64(hash, (uint64_t)entry->precedence);
    hash = confit_v2_ledger_hash_u64(hash, (uint64_t)entry->declaration_order);
    hash = confit_v2_ledger_hash_string(hash, entry->source_path);
    hash = confit_v2_ledger_hash_u64(hash, (uint64_t)entry->source_line);
    hash = confit_v2_ledger_hash_u64(hash, (uint64_t)entry->source_column);
    hash = confit_v2_ledger_hash_value(hash, &entry->value);
  }
  *out_hash = hash;
  return CONFIT_OK;
}
